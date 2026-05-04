#pragma once

#include <basic_coro/awaitable.hpp>
#include "defs.hpp"
#include "ifc/tradable_instrument.hpp"
#include "ifc/types.hpp"
#include "market_instrument.hpp"

#include <bit>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <mutex>
#include <optional>
#include <span>
#include "../utils/serialize.hpp"
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace quarkbot {


    class IStorageTransaction;
    using PStorageTransaction =  std::unique_ptr<IStorageTransaction> ;
    

    using SchemaHash = std::uint64_t;


    ///manages schema hashes and type mapping
    class SchemaHashMapping {
    public:

        ///generate schema hash from type, and write its binary from into DB
        /**
        @tparam type to generate hash
        @param store_cb function called to store hash and binary schema
        @return generated hash
        */
        template<typename T, std::invocable<SchemaHash, std::string> WriteCallback> 
        requires(!std::is_reference_v<T>)
        SchemaHash generate_hash(WriteCallback &&store_cb) {            
            return generate_hash_internal<T, WriteCallback>(&store_cb);
        }   

        ///generate schema hash from type, do not store anything
        /**
        @tparam type to generate hash
        @return generated hash        
        */
        template<typename T> 
        SchemaHash generate_hash() {            
            return generate_hash_internal<T, decltype([](auto,auto){})>(nullptr);
        }   

        
        static SchemaHashMapping instance;

    protected:
        //rember hash and whether data has been stored into database
        std::unordered_map<std::string_view, std::pair<SchemaHash,bool> > _mapping;
        std::mutex _mx;

        template<typename T, std::invocable<SchemaHash, std::string> WriteCallback> 
        requires(!std::is_reference_v<T>)
        SchemaHash generate_hash_internal(WriteCallback *cb) {            
            auto tn = srl::schema_type_name<T>;
            {
                std::lock_guard _(_mx);
                auto iter = _mapping.find(tn);
                if (iter != _mapping.end()) {
                    if (!cb || iter->second.second ) return iter->second.first;
                }
            }    

            srl::BinarySchemaGenerator gen;
            gen.build_schema<T>();
            auto schema = gen.get_schema();
            std::string s;
            srl::BinarySerializer srl([&](std::span<const std::uint8_t> z){
                s.append(reinterpret_cast<const char *>(z.data()), z.size());
            });
            srl(schema);
            std::hash<std::string> hasher;
            SchemaHash h = hasher(s);
            bool stored = cb;
            if (stored) (*cb)(s, h);
            {
                std::lock_guard _(_mx);
                _mapping[tn] = std::pair(h, stored);
            }
            return h;
        }

    };


    inline SchemaHashMapping  SchemaHashMapping::instance = {};

    class IStorage {
    public:
        
        using Buffer = std::string;

        struct Value {
            std::string data;
            bool exists;
            RecordKey key;            
        };

        struct ValueView {
            std::string_view data;
            static constexpr bool exists = true;
            RecordKey key;            
        };

        ///get latest value of the variable
        /** 
        @param variable_name name of variable
        @return value result, always check for exists
        */
        virtual Value get(std::string_view variable_name) const = 0;

        ///get specified value of the variable
        /**
        @param variable_name name of variable
        @param key record key specifies which value to return. Must be exact
        @return value result, always check for exists
        */
        virtual Value get(std::string_view variable_name, const RecordKey &key) = 0;

        ///enumerate all values for given range
        /**
            @param variable_name name of variable
            @param since record key when to start (keys are ordered). You should set random key to 0 (inclusive)
            @param until record key to stop. You should set random key to 0 (exclusive)
        */
        virtual void enumerate(std::string_view variable_name, 
                const RecordKey &since,
                const RecordKey &until,
                function_view<void(const ValueView &val)> callback
            ) const = 0;

        ///List all existing variables
        /**
            @return list of variables
            @note if there are a lot of data in database, the function can be slow. Use only once
            for UI or debugging purpose
        */
        virtual std::vector<std::string> list() const = 0;
        
        
        ///create write transaction
        virtual PStorageTransaction write() = 0;


        virtual ~IStorage() = default;
    };


    class IStorageTransaction {
    public:
        
        ///retrieve associated storage of this transaction
        virtual PStorage get_storage() const = 0; 
        ///commit the write
        /**
        @note state of the transaction after commit is undefined. You need to destroy transaction and recreate its
         */
        virtual void commit() = 0;

        ///Put single value to a variable
        /**
            @param variable_name name of variable
            @param content content of variable
            @return generated RecordKey value

            @note this is effectively put with RecordKey(timestamp,random) - 
            You can get this value by calling get() without record key
        */
        virtual RecordKey put(std::string_view variable_name, std::string_view content) = 0;

        ///Put value under variable and record key
        virtual void put(std::string_view variable_name, const RecordKey &key, std::string_view content) = 0;

        ///Erase all values for single variable
        virtual void erase(std::string_view variable_name) = 0;

        ///Erase single value
        virtual void erase(std::string_view variable_name, const RecordKey &key) = 0;

        virtual ~IStorageTransaction() = default;
    }

#if 0 
    ///Interface for storage of strategy data. 
    /* Storage is used to store strateg3y state, parameters, 
       or any other data that needs to be preserved between runs.    
    */
    class IStorage {
    public:

        ///revision of value, used for sequence keys
        using Revision = std::size_t;

        ///Key for storage value
        struct Key {
            ///key name - must be unique for strategy
            std::string_view name;
            ///if true, storage keeps history of values and allows to retrieve value by revision. If false, only last value is kept and revision is ignored
            bool sequence;

            Key(std::string_view name, bool sequence = true):name(name),sequence(sequence) {}
        };


        ///value stored in storage
        struct Value {
            ///Revision of value, if key is sequence. For non sequence keys, revision is always 0
            Revision rev;
            ///true if value exists, false if key is not found or it is tombstone (deleted value)
            bool exists;
            ///data blob
            std::string data;
            ///return true if value exists, false if it is tombstone or key is not found
            operator bool() const {return exists;}

            ///Extract data to given type
            /** 
            @param var variable which receives data
            @param h receives hash of the schema of stored dat
            @retval true success
            @retval false cannot extract, schema is different, data corrupted
            */

            template<typename T>
            bool extract(T &var, SchemaHash &h) const {
                h = SchemaHashMapping::instance.generate_hash<T>();
                if (extract_schema_hash() != h) return false;
                try {
                    srl::BinaryParser parser([&, pos = std::size_t(0)](std::span<std::uint8_t> to) mutable{
                        if (pos + to.size() > data.length()) throw false;
                        std::copy_n(data.begin()+static_cast<std::ptrdiff_t>(pos), to.size(), to.begin());
                        pos+=to.size();
                    });
                    parser(var);
                    return true;
                } catch (bool) {
                    return false;
                }
            }

            
            ///Extract data to given type
            /** 
            @param var variable which receives data
            @param h receives hash of the schema of stored dat
            @retval true success
            @retval false cannot extract, schema is different, data corrupted
            */
            template<typename T>
            bool extract(T &var) const {
                SchemaHash h;
                return extract(var, h);
            }

            ///Extract schema hash
            SchemaHash extract_schema_hash() const {
                if (data.size()>sizeof(SchemaHash)) {
                    std::array<std::uint8_t, sizeof(SchemaHash)> buff;
                    std::copy_n(data.begin()+static_cast<std::ptrdiff_t>(data.size())-buff.size(), buff.size(), buff.begin());
                    return std::bit_cast<SchemaHash>(buff);                    
                } else {
                    return 0;
                }
            }
        };
        
        virtual ~IStorage() = default;
        ///returns last known value for key. If key is not found, or it is deleted, returned value has exists=false
        virtual Value get(Key key) const = 0;
        ///returns specified revision of value for key. If key is not found, or it is deleted, returned value has exists=false
        virtual Value get(Key key, Revision rev) const = 0;
        ///returns all keys in storage
        /**
            @param filter key filter to apply - you can specify whether you want sequence or non sequence keys, or filter by name prefix. 
            @return vector of all keys matching the filter
         */
        virtual std::vector<std::string> list(const Key &filter) const = 0;        
        ///creates transaction for writing values. Transaction is used to write multiple values atomically. Transaction must be commited by calling commit() method. If transaction is not commited, all changes are discarded
        /**
          @param sync set true to force fsync.
         */
        virtual PStorageTransaction write(bool sync = false) = 0;

        
        ///Retrieve serialized schema
        /**
            @param schema_hash hash
            @return value
        */
        virtual Value get_schema_raw(SchemaHash schema_hash) const = 0;

        ///Retrieve schema from database for given hash
        /**
        @param hash schema hash
        @param schema variable receives the schema
        @retval true success
        @retval false schema not found
         */
        bool get_schema(SchemaHash hash, srl::BinarySchemaGenerator::Schema &schema)  {
            Value tmp = get_schema_raw(hash);
            if (tmp.exists) {
                if (tmp.extract(schema)) {
                    return true;
                }
            } 
            return false;
        }



    };


    class IStorageTransaction {
    public:
        using Key = IStorage::Key;
        using Revision = IStorage::Revision;

        ///put value to storage. 
        /** If key is sequence, value is added as new revision, and revision number is automatically incremented. 
            If key is not sequence, value is overwritten and revision is set to 0
            @param key key to put value for
            @param value_blob data blob to store
            @return revision of stored value. For sequence keys, it is automatically incremented. For non sequence keys, it is always 0
        */
        virtual Revision put(Key key, std::string_view value_blob) = 0;
        ///erase value for key. For sequence keys, it adds tombstone value with next revision. For non sequence keys, it deletes value and sets revision to 0
        /**
            @param key key to erase
            @return revision of tombstone value for sequence keys, or 0 for non sequence keys
         */        
        virtual Revision erase(Key key) = 0;
        ///prune history of key. For sequence keys, it removes all revisions from lowest to "to" (exclusive). For non sequence keys, it does nothing
        /**
            @param key key to prune history for
            @param to ending revision to prune
            @note always keeps last revision, even if it is in range. This means that if "to" is greater than last revision, it is set to last revision
            @note changed API 
         */

        virtual void prune_history(std::string_view key, Revision to) = 0;

        ///Erase one revision of sequential key
        /**
          This erases exact revision of sequential key. It can be useful to erase old indicator data. The function
          can be faster than prune_history, as it doesn't perform range iteration over old keys
         */
        virtual void erase(std::string_view key, Revision rev) = 0;

        ///commit transaction. After commit, all changes are applied to storage. If transaction is not commited, all changes are discarded
        /**
          You should drop transaction object after commit, or if you don't want to commit, to discard changes.
          The state of the transaction after commit is undefined, and should not be used.
         */
        virtual void commit() = 0;

        ///Put schema hash
        /**
        @param hash schema hash
        @param schema serialized schema
        */
        virtual void put_schema(SchemaHash hash, std::string_view schema) = 0;

        virtual ~IStorageTransaction() = default;

        ///Store variable under given key
        /**
            @param key key
            @param data variable's value as serialized type 
            
            @note function uses serializer to serialize data. It also stores schema (only once), and appends schema hash
            to serialized data. The schema can be used to extract data outside of the code

        */
        template<typename T>
        void store(const Key &key, const T &data) {
            SchemaHash h = SchemaHashMapping::instance.generate_hash<T>([&](SchemaHash h, const std::string &bin_schema){
                put_schema(h, bin_schema);
            });

            std::string s;            
            const std::string_view type_name = srl::schema_type_name<T>;
            srl::BinarySerializer srl([&](std::span<const std::uint8_t> z){
                s.append(reinterpret_cast<const char *>(z.data()), z.size());
            });
            srl(data);
            srl(std::bit_cast<std::array<std::uint8_t, sizeof(SchemaHash)> >(h));
            put(key, s);
        }
    };

#endif

    class IStorageManager {
    public:
        ///returns storage for strategy. Storage is used to store strategy state, parameters, or any other data that needs to be preserved between runs. Storage is created on demand, so if it is not found, new storage is created and returned
        /**
            @param name name of storage. It must be unique for strategy.

            @note there can be limit on number of storages, so it is recommended to reuse storages if possible. 
            LevelDB implementation has limit of 127 storages.
                                
         */
        virtual PStorage get_storage(std::string_view name) = 0;
        ///deletes storage with specified name. After deletion, storage is not found and new storage is created 
        /// on demand when get_storage() is called with the same name
        virtual void delete_storage(std::string_view name) = 0;

        ///retrieve list all storages in the database
        virtual std::vector<std::string> list() = 0;

        virtual ~IStorageManager() = default;
    };



}

