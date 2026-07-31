#pragma once

#include "quarkbot/abstract/istorage.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include <array>
#include <bit>
#include <string>
#include <type_traits>
namespace quarkbot {

///persistent variable
/**
    @tparam type of variable. Currently only trivial and strings are supported,
    this can be subject of change in future versions    
    
*/
template<typename T>
class Persistent;



inline StorageTransaction &shared_transaction(Storage storage) {
    static thread_local std::optional<StorageTransaction> trn;
    if (trn) {
        if (trn->get_storage() == storage.get_handle()) {
            return trn.value();
        }
        trn->commit();
    } else {
        ExecutionWorker worker = ExecutionWorker::current();
        worker.required();
        auto flush_coro = []() -> StrategyFragment {
            if (trn) {
                trn->commit();
                trn = {};
            }
            co_return;
        };

        worker.run(flush_coro());
    }
    trn = storage.write();
    return trn.value();
}


///Define persistent namespace
/**
    Namespace declaration - you can bind a variable to a namespace
    This object just defines namespace in given storage
*/
class PersistentNamespace {
public:
    ///COnstruct namespace
    /**
        @param storage storage
        @param name name of the namespace
    */
    PersistentNamespace(Storage storage,std::string name):_storage(std::move(storage)), _name(std::move(name)) {}

    ///get storage
    Storage get_storage() const {return _storage;}
    ///get name of the namespace
    const std::string &get_name() const {return _name;}

    ///retrun new namespace under this namespace (sub-namespace)
    PersistentNamespace sub_ns(std::string_view name) const {
        return {*this, name};
    }

protected:
    Storage _storage;
    std::string _name;
    
    PersistentNamespace(const PersistentNamespace &other_group, std::string_view name)
        :_storage(other_group.get_storage()), _name(std::format("{}::{}",other_group._name,name)) {}

};


template<typename T>
requires (std::is_trivially_copyable_v<T>)
class Persistent<T> {
public:

    ///Construct persistent variable with default value
    /**
        @param storage storage
        @param name name
        @param def_value default value (if no value is stored)
    */
    Persistent(Storage storage, std::string name, T def_value = {})
        :group(std::move(storage), std::move(name)), value (std::move(def_value)) {
            init();
        }
    ///Construct persistend variable in speccified namespace
    /**
        @param ns namespace
        @param name of variable
        @param def_value default value (if no value is stored)
    */
    Persistent(const PersistentNamespace &ns, std::string name, T def_value = {})
        :group(ns.sub_ns(name)), value (std::move(def_value)) {
            init();
        }

    ///Can't be copied
    Persistent(const Persistent &) = delete;
    ///Can be moved
    Persistent(Persistent &&) = default;
    ///assign - copy value
    Persistent &operator=(const Persistent &other) {
        if (this != &other)  {
            set(other.value);
        }
        return *this;
    }
    //assign - move value
    Persistent &operator=(Persistent &&other) {
        if (this != &other)  {
            set(std::move(other.value));
        }
        return *this;
    }
    //assign - set value
    Persistent &operator=(const T &other) {
        set(other);
        return *this;
    }

    //assign - move value
    Persistent &operator=(T &&other) {
        set(std::move(other));
        return *this;
    }

    ///set value
    /**
    @param val value
    @note actual commit into database can be delayed 
     */
    void set(const T &val) {
        if (val != value) {
            value = val;
            store();
        }
    }
    ///set value
    /**
    @param val value
    @note actual commit into database can be delayed 
     */
    void set(T &&val) {
        if (val != value) {
            value = std::move(val);
            store();
        }
    }

    ///get current value
    operator T() const {
        return value;
    }

    ///get current value
    T get() const {
        return value;
    }
    

protected:
    PersistentNamespace group;
    T value = {};

    void init() {
        auto val = group.get_storage().get(group.get_name());
        if (val.exists) {
            if (val.data.size() == sizeof(T)) {
                std::array<char, sizeof(T)> bin;
                std::copy(val.data.begin(), val.data.end(), bin.begin());
                value = std::bit_cast<T>(bin);                
            }
        }
    }

    void store() {        
        auto &trn = shared_transaction(group.get_storage());
        auto bin = std::bit_cast<std::array<char, sizeof(T)> >(value);
        trn.put(group.get_name(), std::string_view(bin.begin(), bin.end()));
    }
};

template<>
class Persistent<std::string> {
public:
    Persistent(Storage storage, std::string name, std::string def_value)
        :group(std::move(storage), std::move(name)), value (std::move(def_value)) {
            init();
        }
    Persistent(Storage storage, std::string name)
        :group(std::move(storage), std::move(name)) {
            init();
        }
    Persistent(const PersistentNamespace &group, std::string name, std::string def_value)
        :group(group.sub_ns(name)), value (std::move(def_value)) {
            init();
        }
    Persistent(const PersistentNamespace &group, std::string name)
        :group(group.sub_ns(name)) {
            init();
        }

    Persistent(const Persistent &) = delete;
    Persistent(Persistent &&) = default;
    Persistent &operator=(const Persistent &other) {
        if (this != &other)  {
            set(other.value);
        }
        return *this;
    }
    Persistent &operator=(Persistent &&other) {
        if (this != &other)  {
            set(std::move(other.value));
        }
        return *this;
    }
    Persistent &operator=(const std::string &other) {
        set(other);
        return *this;
    }
    Persistent &operator=(std::string &&other) {
        set(std::move(other));
        return *this;
    }

    void set(const std::string &val) {
        if (value != val) {
            value = val;
            store();
        }
    }
    void set(std::string &&val) {
        if (val != value) {
            value = std::move(val);
            store();
        }
    }
    operator std::string_view() const {
        return value;
    }
    operator std::string() const {
        return value;
    }
    const std::string &get() const {
        return value;
    }

protected:
    PersistentNamespace group;
    std::string value = {};

    void init() {
        auto val = group.get_storage().get(group.get_name());
        if (val.exists) {
            value = val.data;
        }
    }

    void store() {
        auto &trn = shared_transaction(group.get_storage());
        trn.put(group.get_name(), value);
    }
};

}