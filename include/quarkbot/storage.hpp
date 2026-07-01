#pragma once

#include <basic_coro/awaitable.hpp>
#include "defs.hpp"
#include "types.hpp"
#include "serializer/schema.hpp"
#include "serializer/serialize.hpp"
#include "utils/signals.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace quarkbot {


    class IStorageTransaction;
    using PStorageTransaction =  std::unique_ptr<IStorageTransaction> ;
    


    class IStorage {
    public:
        
        using Buffer = std::string;
        using WatcherSlot = signals::SignalSlot<void(IStorageTransaction &tx, const std::string_view &variable, const RecordKey &key, std::optional<std::string_view> content)>;

        struct Extractor {

            template<typename Self, typename T>
            bool extract(this Self &&self, T &val, SchemaHash &h) {
                if (!self.exists || self.data.size() < sizeof(SchemaHash)) return false;

                std::array<char, sizeof(SchemaHash)> buff;
                std::string_view value(self.data);
                std::string_view tail = value.substr(value.size() - sizeof(SchemaHash));
                std::copy(tail.begin(), tail.end(),buff.begin());
                
                SchemaHash record_hash = std::bit_cast<SchemaHash>(buff);
                
                auto th = schema_hash_for_type<T>;
                if (!th.has_value()) {
                    std::string schema_bin = serialize_schema<T>();
                    std::hash<std::string> hasher;
                    th = hasher(schema_bin);
                }

                h = th.value();

                if (record_hash != h) return false;

                bool valid = false;
                try {
                    srl::BinaryParser parser([&, pos = std::size_t(0)](std::span<std::uint8_t> buff) mutable {
                        if (pos + value.size() > value.size()) throw true;
                        std::copy_n(value.begin()+static_cast<std::ptrdiff_t>(pos), buff.size(), buff.begin());
                        pos += buff.size();
                    });
                    parser(val);
                    valid = true;
                } catch (...) {
                    valid = false;
                }
                return valid;
            }

            template<typename Self, typename T>
            bool extract(this Self &&self, T &val) {
                SchemaHash h;
                return self.extract(val,h);
            }

            template<typename Self, typename T>
            bool operator >> (this Self &&self, T &val) {
                return self.extract(val);
            }
        };


        struct Value : Extractor{
            std::string data;
            bool exists;
            RecordKey key;            
        };

        struct ValueView : Extractor{
            static constexpr bool exists = true;
            std::string_view data;
            RecordKey key;            
            operator Value() const {
                return Value{{}, std::string(data), true, key};
            }
        };

        using Enumerator = std::function<bool(ValueView &)>;

        class Iterator {
        public:
            using iterator_category = std::input_iterator_tag;
            using value_type = Value;
            using difference_type = std::ptrdiff_t;
            using pointer = void;
            using reference = ValueView;

            Iterator() = default;
            Iterator(Enumerator enumerator): _enumerator(std::move(enumerator)) {
                //assume enumerator is always valid and not empty, so we can call it immediately
                _is_end = !_enumerator(_current);
            }
            reference operator*() const {
                return _current;
            }
            Iterator &operator++() {                
                if (_is_end) return *this;
                _is_end = !_enumerator(_current);   
                return *this;
            }
            void operator++(int) { this->operator++(); }
            bool operator==(const Iterator &other) const {
                return _is_end == other._is_end && (_is_end || _current.key == other._current.key);
            }

        protected:
            Enumerator _enumerator;
            ValueView _current;
            bool _is_end = true;
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
        virtual Value get(std::string_view variable_name, const RecordKey &key) const = 0;

        ///Retrieve iterator 
        /**
        @param variable_name name of variable to iteratoe
        @param since start record (inclusive)
        @param until end record (exclusive)
        @return iterator (never returns empty)
        @note Use ranges, see select_range
        @note if since and until are reversed, performs reverse iteration
        @see select_range
         */
        virtual Enumerator get_enumerator(std::string_view variable_name, const RecordKey &since, const RecordKey &until) const =0;
        

        ///Creates range object to iterate range of values
        /**
            @param variable_name name of variable to iterate
            @param since start record (inclusive)
            @param until end record (exclusive)
            @note if since and until are reversed, performs reverse iteration
            @return range object - even empty if variable doesn't exists or range is not defined
         */
        auto select_range(std::string_view variable_name, const RecordKey &since, const RecordKey &until) const {
            auto en= get_enumerator(variable_name, since, until);
            return std::ranges::subrange(Iterator(en), Iterator());
        }
        
        ///List all existing variables
        /**
            @param prefix filter list for given prefix
            @return list of variables
            @note if there are a lot of data in database, the function can be slow. Use only once
            for UI or debugging purpose
        */
        virtual std::vector<std::string> list(std::string_view prefix = {}) const = 0;
        
        ///Retrieve binary version of schema
        virtual Value get_schema_binary(SchemaHash h) const = 0;

        ///create write transaction
        virtual PStorageTransaction write() = 0;

        
        ///Retrieve schema from database
        /**
        @param h schema hash. This hash can be retrieved as last 8 bytes of the value
        @return schema if exists        
        */
        std::optional<srl::BinarySchemaGenerator::Schema> get_schema(SchemaHash h) const  {
            std::optional<srl::BinarySchemaGenerator::Schema> out;
            out.emplace();
            if (!(get_schema_binary(h) >> out.value())) {
                out.reset();
            }
            return out;
            
        }

        ///Retrieve current namespace if defined (default is not defined)
        virtual std::string_view get_namespace() const {return {};}

        ///Retrieve root storage. 
        /**
        @note the function can return nullptr, which means, that this is root storage
        */
        virtual PStorage get_root_storage() const {return  {};}

        virtual void add_precommit_hook_connection(WatcherSlot::Connection consumer) = 0;

        template<std::invocable<const IStorageTransaction &, const std::string_view &, const RecordKey &, std::optional<std::string_view>  > Fn>
        WatcherSlot::Connection add_precommit_hook( Fn &&consumer) {
            auto conn = WatcherSlot::create_connection(std::move(consumer));
            add_precommit_hook_connection(conn);
            return conn;
        }

        virtual ~IStorage() = default;

        ///Create namespace 
        /**
            Creates new storage as namespace of this storage. All keys in namespaced storage
            are prefixed by prefix
            @param root smart pointer to storage (PStorage) which can be used as root. It
            is allowed to put already namespaced storage. In this case, you create namespace
            inside namespace. Note that function get_root_storage() still returns real root
            storage, not parent storage.

            @param prefix prefix for namespace. Note use some separator. For example, 
            this is valid prefix "my_namespace/" (separator /)

            @note root storage can see all keys, 

            @return storage for namespace

            @note namespace is not MT safe!. If you need to access namespace from multiple execution
            workers or threads, create namespace object for each thread. You can create multiple
            objects for single namespace.

            @note ensure, you include "storage_namespace.hpp". You just need to include this
            into source which creates the storage, it doesn't need to be in source which
            uses the storage

        */
        static PStorage create_namespace(PStorage root, std::string_view prefix) ;
    };

    enum class UpdateLastRevision {
        enable,
        disable
    };

    class IStorageTransaction {
    public:
        
        ///retrieve associated storage of this transaction
        virtual PStorage get_storage() const = 0; 
        ///commit the write
        /**
        @note state of the transaction after commit is undefined. You need to destroy transaction and recreate its
        @param sync set true causes that data are immediately copied to external storage. If false, OS caching
        can delay the write.
         */
        virtual void commit(bool sync = false) = 0;

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
        /**
        @param variable_name name of variable
        @param key record key
        @param content content to store
        @param update_last_revision set to disable, to skip update last revision. This make write slightly faster, but
            changes behaviour of get() without recordkey. Set only if your code doesn't use that version of function get()
        */
        virtual void put(std::string_view variable_name, const RecordKey &key, std::string_view content, 
                UpdateLastRevision update_last_revision = UpdateLastRevision::enable) = 0;

        ///Erase all values for single variable
        virtual void erase(std::string_view variable_name) = 0;

        ///Erase single value
        virtual void erase(std::string_view variable_name, const RecordKey &key) = 0;

        ///Puts schema to database as binary
        virtual void put_schema_binary(SchemaHash hash, std::string_view binary) = 0;


        ///Serialize value, store schema
        /**
            @param val value to serialize - must be serializable
            @return binary version of the value
            @note Function also serializes schema into database. This is done once per application life time and type T
        */
        template<typename T>
        std::string serialize_value(const T &val) {
            std::vector<char> buffer;
            buffer.reserve(sizeof(val));
            srl::BinarySerializer serializer([&](std::span<const std::uint8_t> data){
                buffer.insert(buffer.end(), data.begin(), data.end());
            });
            serializer(val);
            auto hash = schema_hash_for_type<T>;
            if (!hash.has_value() || !schema_is_stored<T>) {
                std::string binschema = serialize_schema<T>();
                std::hash<std::string> hasher;
                hash = schema_hash_for_type<T> = hasher(binschema);
                schema_is_stored<T> = true;
                put_schema_binary(hash.value(), binschema);
            }
            auto hash_bin = std::bit_cast<std::array<char, sizeof(SchemaHash)> >(hash.value());
            buffer.insert(buffer.end(), hash_bin.begin(), hash_bin.end());            
            return {buffer.begin(), buffer.end()};
        }

        ///Store value under variable
        /**
            @param variable_name name of variable
            @param val value

            @note final format of value is <binary><schemehash> . You need to use extract to convert binary back to value
            @return RecordKey of new value. 
            
            @note function doesn't delete old value. 
        */
        template<typename T>
        RecordKey store(std::string_view variable_name, const T &val) {
            return put(variable_name, serialize_value(val));            
        }


        ///Store value under variable
        template<typename T>
        void store(std::string_view variable_name, const RecordKey &key,  const T &val, UpdateLastRevision update_last_revision = UpdateLastRevision::enable) {
            return put(variable_name,key, serialize_value(val),update_last_revision);            
        }


        virtual ~IStorageTransaction() = default;
    };

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

