#pragma once

#include "quarkbot/abstract/istorage.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/serializer/serialize.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/storage_srl.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include <array>
#include <bit>
#include <string>
#include <type_traits>
namespace quarkbot {


enum class CommitStrategy {
    ///delayed commit strategy -
    /** commited to storage later by using separate task, fastest, but writes need to be in an active Execution worker 
        Writes are not immediately visible in storage.
    */
    delayed,
    ///Immediately write
    /**
        Write to this variable is immediately stored into the storage. Use it if you need to write 
        outside of Execution worker. The write can be slower than delayed 
    */
    immediately,
    ///Imeddiately write and enforce synchronization with the storage
    /**
        Slowest, but ensures, that write is stored on the disk
    */
    immediately_sync
};
///persistent variable
/**
    @tparam type of variable. Currently only trivial and strings are supported,
    this can be subject of change in future versions    
    
*/
template<typename T, CommitStrategy cs = CommitStrategy::delayed>
class Persistent;



inline StorageTransaction &shared_transaction(Storage storage, CommitMode cmode) {
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

        worker.run_on_idle(flush_coro());
    }
    trn = {};
    trn = storage.write(cmode);
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


template<typename T, CommitStrategy cs>
requires (srl::SerializeRuleExists<T>)
class Persistent<T,cs> {
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
        val.extract(value);
    }

    void store() {        
        if constexpr(cs == CommitStrategy::delayed) {
            auto &trn = shared_transaction(group.get_storage(), CommitMode::lazy);
            trn.store(group.get_name(), value);
        } else {
            auto trn = group.get_storage().write(cs == CommitStrategy::immediately_sync ? CommitMode::sync : CommitMode::local);
            trn.store(group.get_name(), value);
            trn.commit();
        }
    }
};


}