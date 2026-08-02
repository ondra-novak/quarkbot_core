#pragma once

#include "../defs.hpp"
#include "../serializer/schema_fwd.hpp"
#include "../types.hpp"
#include "../utils/signals.hpp"
#include <functional>
#include <memory>
#include <stdexcept>
namespace quarkbot {

    class IStorageTransaction;
    using PStorageTransaction =  std::unique_ptr<IStorageTransaction> ;

    ///Direction in which a record range is traversed
    /** The bounds of the range are given in the order of travel, so the direction
        restates them. A mismatch yields an empty range - see IStorage::get_enumerator */
    enum class RangeDirection {
        ///from the lower bound up; requires from < to
        ascending,
        ///from the upper bound down; requires from > to
        descending
    };

    class IStorage {
    public:
        
        ///Describes single record change performed by a committed transaction
        /**
            The event carries a *logical* key - it never contains any backend specific
            keyspace or instance prefix. This makes events portable: they can be applied
            to a different backend, or to a different logical keyspace of the same backend,
            via IStorageTransaction::put(const ReplicatorEvent &).
        */
        struct ReplicatorEvent {


            std::string_view key;
            std::string_view value;
            bool erase;
            bool schema_hash;
        };

        using Buffer = std::string;    
        using Replicator = signals::SignalSlot<void(ReplicatorEvent)>;

        struct Extractor {

            template<typename Self, typename T>
            bool extract(this Self &&self, T &val, srl::SchemaHash &h);

            template<typename Self, typename T>
            bool extract(this Self &&self, T &val) {
                srl::SchemaHash h;
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
        @param variable_name name of variable to iterate
        @param from first bound, in the order of travel
        @param to second bound, in the order of travel
        @param dir direction of travel, which must agree with the order of the bounds
        @return iterator (never returns empty)

        @note The bounds always delimit the half-open range [lower, upper) - the lower
        bound is included and the upper one excluded regardless of direction. Iterating
        descending therefore excludes `from` and includes `to`. This keeps a range
        selecting the same set of records in both directions, and lets a range over
        ordered values <a,b> be written as [RecordKey::first(a), RecordKey::after(b))
        whichever way it is traversed.

        @note `dir` restates what the order of the bounds already says. If the two
        disagree - or the bounds are equal - the range is empty rather than silently
        iterating the other way.

        @note Use ranges, see select_range
        @see select_range
         */
        virtual Enumerator get_enumerator(std::string_view variable_name, const RecordKey &from,
                const RecordKey &to, RangeDirection dir) const =0;
        
        
        ///List all existing variables
        /**
            @param prefix filter list for given prefix
            @return list of variables
            @note if there are a lot of data in database, the function can be slow. Use only once
            for UI or debugging purpose
        */
        virtual std::vector<std::string> list(std::string_view prefix = {}) const = 0;
        
        ///Retrieve binary version of schema
        virtual Value get_schema_binary(srl::SchemaHash h) const = 0;

        ///Determines whether schema is stored without retrieveing it
        /**
            The call should be fast!
            The value can be cached in some lookup map, because once the schema is stored, it is stored forever
            The function is called for every put with schema and it determines whether to serialize and store schema
        */
        virtual bool is_schema_stored(srl::SchemaHash h) const = 0;

        ///create write transaction
        virtual PStorageTransaction write() = 0;

      
        ///Retrieve current namespace if defined (default is not defined)
        virtual std::string_view get_namespace() const {return {};}

        ///Retrieve root storage. 
        /**
        @note the function can return nullptr, which means, that this is root storage
        */
        virtual PStorage get_root_storage() const {return  {};}

        virtual void add_replicator(Replicator::Connection replicator) = 0;
        


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

        class Null;
    };

    enum class UpdateLastRevision {
        ///update last revision
        enable,
        ///do not update last revision
        disable,
        ///update last revision and also erase previous revision
        enable_erase_last
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
        virtual void put_schema_binary(srl::SchemaHash hash, std::string_view binary) = 0;

        ///Replicate from different database
        virtual void put(const IStorage::ReplicatorEvent &event) = 0;
        ///Serialize value, store schema
        /**
            @param val value to serialize - must be serializable
            @return binary version of the value
            @note Function also serializes schema into database. This is done once per application life time and type T
        */
  
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

        class Null;
    };


    class IStorage::Null final : public IStorage {
    public:
        virtual Value get(std::string_view ) const override {
            return {{},{}, false, {}};
        }
        virtual Value get(std::string_view , const RecordKey &key) const override{
            return {{},{}, false, key};
        }
        virtual Enumerator get_enumerator(std::string_view , const RecordKey &, const RecordKey &, RangeDirection) const override{
            return [](ValueView &){return false;};
        }
        virtual std::vector<std::string> list(std::string_view ) const override{
            return {};
        }
        virtual Value get_schema_binary(srl::SchemaHash) const override{
            return {{},{}, false, {}};
        }
        virtual PStorageTransaction write() override{
            throw std::runtime_error("Cannot open transaction:  Storage is read only");            
        }
        virtual void add_replicator(Replicator::Connection) override {
            //read only, do nothing
        }
        virtual bool is_schema_stored(srl::SchemaHash) const override {
            return true;
        }
    };


    class IStorageManager::Null final: public IStorageManager {
    public:
        virtual PStorage get_storage(std::string_view )override {return {}; }       
        virtual void delete_storage(std::string_view ) override{}
        virtual std::vector<std::string> list() override {return {};}
    };



    
    
}