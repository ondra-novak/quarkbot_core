#pragma once

#include "abstract/istorage.hpp"
#include "utils/signals.hpp"
#include "defs.hpp"
#include "utils/wrapper.hpp"
#include <memory>

namespace quarkbot {

class StorageTransaction;

template<typename T>
class PersistentSerie;

class Storage: public Wrapper<IStorage> {
public:

    using Wrapper<IStorage>::Wrapper;

    using Value = IStorage::Value;
    using ValueView = IStorage::ValueView;
    using Iterator = IStorage::Iterator;
    using Enumerator = IStorage::Enumerator;
    using WatcherSlot = IStorage::WatcherSlot;

    ///get latest value of the variable
    /** 
    @param variable_name name of variable
    @return value result, always check for exists
    */
    Value get(std::string_view variable_name) const {return _ptr->get(variable_name);}

    ///get specified value of the variable
    /**
    @param variable_name name of variable
    @param key record key specifies which value to return. Must be exact
    @return value result, always check for exists
    */
    Value get(std::string_view variable_name, const RecordKey &key) const {return _ptr->get(variable_name, key);}

    ///Creates range object to iterate range of values
    /**
    @param variable_name name of variable to iterate
    @param since start record (inclusive)
    @param until end record (exclusive)
    @note if since and until are reversed, performs reverse iteration
    @return range object - even empty if variable doesn't exists or range is not defined
    */
    auto select_range(std::string_view variable_name, const RecordKey &since, const RecordKey &until) const {
        auto en= _ptr->get_enumerator(variable_name, since, until);
        return std::ranges::subrange(Iterator(std::move(en)), Iterator());
    }

    ///List all existing variables
    /**
        @param prefix filter list for given prefix
        @return list of variables
        @note if there are a lot of data in database, the function can be slow. Use only once
        for UI or debugging purpose
    */
    std::vector<std::string> list(std::string_view prefix = {}) const {return _ptr->list(prefix);}

    ///Retrieve binary version of schema
    Value get_schema_binary(srl::SchemaHash h) const {return _ptr->get_schema_binary(h);}

    /**
    @param h schema hash. This hash can be retrieved as last 8 bytes of the value
    @return schema if exists        
    @note to call this method you need to include companion header storage_srl.hpp
    */
    auto get_schema(srl::SchemaHash h) const;

    StorageTransaction write();

    ///Retrieve current namespace if defined (default is not defined)
    std::string_view get_namespace() const {return _ptr->get_namespace();}

    ///Retrieve root storage. 
    Storage get_root_storage() const {
        auto x = _ptr->get_root_storage();
        if (x) return Storage(std::move(x));
        else return *this;
    }

    template<std::invocable<const IStorageTransaction &, const std::string_view &, const RecordKey &, std::optional<std::string_view>  > Fn>
    WatcherSlot::Connection add_precommit_hook( Fn &&consumer) {
        auto conn = WatcherSlot::create_connection(std::move(consumer));
        _ptr->add_precommit_hook_connection(conn);
        return conn;
    }

    template<typename T>
    PersistentSerie<T> get_serie(std::string_view variable_name);



};

class StorageTransaction {
public:
    StorageTransaction(PStorageTransaction ptr):_ptr(std::move(ptr)) {};

    ///retrieve associated storage of this transaction
    Storage get_storage() const {return {_ptr->get_storage()};}

    ///commit the write
    /**
    @note state of the transaction after commit is undefined. You need to destroy transaction and recreate its
    @param sync set true causes that data are immediately copied to external storage. If false, OS caching
    can delay the write.
    */
    void commit(bool sync = false) {_ptr->commit(sync);}

    ///Put single value to a variable
    /**
        @param variable_name name of variable
        @param content content of variable
        @return generated RecordKey value

        @note this is effectively put with RecordKey(timestamp,random) - 
        You can get this value by calling get() without record key
    */
    RecordKey put(std::string_view variable_name, std::string_view content) {
        return _ptr->put(variable_name,content);
    }

    ///Put value under variable and record key
    /**
    @param variable_name name of variable
    @param key record key
    @param content content to store
    @param update_last_revision set to disable, to skip update last revision. This make write slightly faster, but
    changes behaviour of get() without recordkey. Set only if your code doesn't use that version of function get()
    */
    void put(std::string_view variable_name, const RecordKey &key, std::string_view content, 
        UpdateLastRevision update_last_revision = UpdateLastRevision::enable) {
            return _ptr->put(variable_name, key, content, update_last_revision);
    }

    ///Erase all values for single variable
    void erase(std::string_view variable_name) {
        _ptr->erase(variable_name);
    }

    ///Erase single value
    void erase(std::string_view variable_name, const RecordKey &key) {
        _ptr->erase(variable_name, key);
    }

    ///Puts schema to database as binary
    void put_schema_binary(srl::SchemaHash hash, std::string_view binary) {
        _ptr->put_schema_binary(hash, binary);
    }


    ///Serialize value, store schema
    /**
        @param val value to serialize - must be serializable
        @return binary version of the value
        @note Function also serializes schema into database. This is done once per application life time and type T
    */
    template<typename T>
    std::string serialize_value(const T &val); //method is defined in a companion header

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


protected:
    PStorageTransaction _ptr;
};

inline StorageTransaction Storage::write() {
    return _ptr->write();
}

class StorageManager : public Wrapper<IStorageManager> {
public:
    using Wrapper<IStorageManager>::Wrapper;
    ///returns storage for strategy. Storage is used to store strategy state, parameters, or any other data that needs to be preserved between runs. Storage is created on demand, so if it is not found, new storage is created and returned
    /**
        @param name name of storage. It must be unique for strategy.

        @note there can be limit on number of storages, so it is recommended to reuse storages if possible. 
        LevelDB implementation has limit of 127 storages.
                            
        */
    Storage get_storage(std::string_view name) const {
        auto r = _ptr->get_storage(name);
        if (r) return Storage(r);
        return Storage{};
    }

    ///deletes storage with specified name. After deletion, storage is not found and new storage is created 
    /// on demand when get_storage() is called with the same name
    void delete_storage(std::string_view name) {
        _ptr->delete_storage(name);
    }

    ///retrieve list all storages in the database
     std::vector<std::string> list() const {
        return _ptr->list();
     }

};


}

