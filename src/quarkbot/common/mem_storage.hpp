#pragma once

#include "quarkbot/abstract/istorage.hpp"
#include "storage_common.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/utils/bigendian.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

namespace quarkbot {

class MemStorage;


class MemStorageTransaction final : public IStorageTransaction {
public:
    explicit MemStorageTransaction(std::shared_ptr<MemStorage> storage) : _storage(storage) {}

            ///retrieve associated storage of this transaction
    virtual PStorage get_storage() const  override;
    virtual void commit() override;
    virtual RecordKey put(std::string_view variable_name, std::string_view content) override;
    virtual void put(std::string_view variable_name, const RecordKey &key, std::string_view content,
        UpdateLastRevision update) override;
    virtual void apply(const IStorage::ReplicatorEvent &event) override;
    virtual void erase(std::string_view variable_name) override;
    virtual void erase(std::string_view variable_name, const RecordKey &key) override;
    virtual void put_schema_binary(srl::SchemaHash hash, std::string_view binary) override;    


private:
    struct OpPut   { std::string variable; RecordKey key; std::string data; UpdateLastRevision mode ;};
    struct OpErase { std::string variable; };
    struct OpReplicate { IStorage::ReplicatorEvent::Type type; std::string name;
                         RecordKey recordkey; std::string value; srl::SchemaHash schema_hash; };
    struct OpEraseRev { std::string variable; RecordKey rev; };
    struct OpPutSchema { srl::SchemaHash hash; std::string schema; };
    using Op = std::variant<OpPut, OpErase, OpEraseRev, OpPutSchema, OpReplicate>;

    std::vector<Op> _ops;
    std::shared_ptr<MemStorage> _storage;

    friend class MemStorage;
};

class MemStorage final : public IStorage, public std::enable_shared_from_this<MemStorage> {
    friend class MemStorageTransaction;
public:
    enum HistoryMode {
        keep_history,
        no_history
    };

        virtual Value get(std::string_view variable_name) const override;
        virtual Value get(std::string_view variable_name, const RecordKey &key) const override;
        virtual Enumerator get_enumerator(std::string_view variable_name, const RecordKey &from, const RecordKey &to, RangeDirection dir) const override;
        virtual std::vector<std::string> list(std::string_view prefix ) const override;
        virtual Value get_schema_binary(srl::SchemaHash h) const override;
        virtual PStorageTransaction write(CommitMode mode) override;
        virtual void add_replicator(Replicator::Connection consumer) override {
            connect(_watcher, consumer);
        }
        virtual bool is_schema_stored(srl::SchemaHash h) const override {
            return _schemas.contains(h);
        }

        MemStorage(HistoryMode mode = keep_history):_no_history_for_simple_variables(mode == no_history) {}

        Replicator &get_watcher() {return _watcher;}

    static PStorage create(HistoryMode mode = keep_history) {
        return std::make_shared<MemStorage>(mode);
    }
    

private:    
    std::map<std::string, std::string, std::less<> > _storage;
    std::map<srl::SchemaHash, std::string> _schemas;
    Replicator _watcher;
    bool _no_history_for_simple_variables = false;

    void apply(MemStorageTransaction::OpErase &&x);
    void apply(MemStorageTransaction::OpEraseRev &&x);
    void apply(MemStorageTransaction::OpPut &&x);
    void apply(MemStorageTransaction::OpPutSchema &&x);
    void apply(MemStorageTransaction::OpReplicate &&x);
    

    static std::uint64_t random_key_counter ;

};

inline  std::uint64_t MemStorage::random_key_counter ;

inline PStorage MemStorageTransaction::get_storage() const  {
    return _storage;
}
inline void MemStorageTransaction::commit() {
    for (auto &x: _ops) {
        std::visit([&](auto &x) {
            _storage->apply(std::move(x));
        }, x);
    }
    _ops.clear();
}


inline RecordKey MemStorageTransaction::put(std::string_view variable_name, std::string_view content) {
    RecordKey rc { static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()), MemStorage::random_key_counter++};
    MemStorageTransaction::put(variable_name, rc, content, _storage->_no_history_for_simple_variables?UpdateLastRevision::enable_erase_last: UpdateLastRevision::enable);
    return rc;
}
inline void MemStorageTransaction::put(std::string_view variable_name, const RecordKey &key, std::string_view content, UpdateLastRevision update) {
    _ops.push_back(OpPut{std::string(variable_name), key, std::string(content), update });

}
inline void MemStorageTransaction::erase(std::string_view variable_name) {
    _ops.push_back(OpErase{std::string(variable_name)});
}
inline void MemStorageTransaction::erase(std::string_view variable_name, const RecordKey &key) {
    _ops.push_back(OpEraseRev{std::string(variable_name), key});
}
inline void MemStorageTransaction::put_schema_binary(srl::SchemaHash hash, std::string_view binary) {
    _ops.push_back(OpPutSchema{hash, std::string(binary)});
}

inline MemStorage::Value MemStorage::get(std::string_view variable_name) const {
    auto iter = _storage.find(variable_name);
    if (iter == _storage.end() || iter->second.size() != sizeof(RecordKey)) return {{},{},false,{}};
    return MemStorage::get(variable_name, string_to_record_key(iter->second));
    
}


inline MemStorage::Value MemStorage::get(std::string_view variable_name, const RecordKey &key) const {
    Value out = { {}, {}, {} , key};
    auto iter = _storage.find(wholeKey(variable_name, key));
    if (iter != _storage.end()) {
        out.data = iter->second;
        out.exists = true;
    }
    return out;


}
inline MemStorage::Enumerator MemStorage::get_enumerator(std::string_view variable_name,
        const RecordKey &from, const RecordKey &to, RangeDirection dir) const  {

    constexpr auto empty = [](ValueView &){return false;};
    const bool ascending = dir == RangeDirection::ascending;
    //the direction only restates the order of the bounds - a disagreement is a mistake
    //on the caller's side, not a request to iterate the other way
    if (ascending ? !(from < to) : !(from > to)) return empty;

    //bounds of the half-open range [lower, upper), computed once for both directions
    auto b = _storage.lower_bound(wholeKey(variable_name, ascending?from:to));
    auto e = _storage.lower_bound(wholeKey(variable_name, ascending?to:from));
    // +1 skips the '\0' separator between variable_name and the big-endian RecordKey bytes
    auto sz = variable_name.size() + 1;

    constexpr auto load = [](ValueView &w, auto iter, std::size_t sz) {
            w.key = string_to_record_key(std::string_view(iter->first).substr(sz));
            w.data = iter->second;
    };

    if (ascending) {
        return [load, b, e, sz](ValueView &w)mutable -> bool {
            if (b == e) return false;
            load(w, b, sz);
            ++b;
            return true;
        };
    } else {
        //e walks down to b; decrementing is safe because e != b implies e != begin()
        return [load, b, e, sz](ValueView &w) mutable -> bool {
            if (e == b) return false;
            --e;
            load(w, e, sz);
            return true;
        };
    }
}
inline std::vector<std::string> MemStorage::list(std::string_view prefix ) const {
    std::string root;
    std::vector<std::string> out;
    for (auto iter = prefix.empty()?_storage.begin():_storage.lower_bound(prefix); iter != _storage.end(); ++iter) {
        const auto &k = iter->first;        
        if (!prefix.empty() && k.substr(0,prefix.size()) != prefix) break;
        // data entries: variable_name + '\0' + RecordKey(16 bytes)
        // last-revision pointers: variable_name only — skip them
        if (k.size() <= sizeof(RecordKey)) continue;
        if (k[k.size() - sizeof(RecordKey) - 1] != '\0') continue;
        auto varname = k.substr(0, k.size() - sizeof(RecordKey) - 1);
        if (varname != root) {
            out.push_back(varname);
            root = varname;
        }
    }
    return out;
}
inline MemStorage::Value MemStorage::get_schema_binary(srl::SchemaHash h) const {
    auto iter = _schemas.find(h);
    if (iter == _schemas.end()) {
        return {{},{},false,{}};
    }
    return {{}, iter->second, true, {}};
}
inline PStorageTransaction MemStorage::write(CommitMode) {
    return std::make_unique<MemStorageTransaction>(shared_from_this());
}

inline void MemStorage::apply(MemStorageTransaction::OpErase &&x) {
    using Type = ReplicatorEvent::Type;
    _storage.erase(x.variable);
    std::string beg = x.variable + '\0';
    std::string end = x.variable + '\x01';
    _storage.erase(_storage.lower_bound(beg), _storage.lower_bound(end));
    //the caller asked for the whole variable, so say that once - a replica backed by a
    //relational store turns it into a single DELETE ... WHERE name = ?. A LevelDB source
    //cannot do this: its committed batch holds per-record deletions and nothing else.
    _watcher(ReplicatorEvent{.type = Type::erase_name, .name = x.variable});
}
inline void MemStorage::apply(MemStorageTransaction::OpEraseRev &&x) {
    using Type = ReplicatorEvent::Type;
    std::string key = wholeKey(x.variable, x.rev);
    _storage.erase(key);
    _watcher(ReplicatorEvent{.type = Type::erase_key, .name = x.variable, .recordkey = x.rev});
}
inline void MemStorage::apply(MemStorageTransaction::OpPut &&x) {
    using Type = ReplicatorEvent::Type;
    auto tmp = record_key_to_string(x.key);
    //the previous revision, when enable_erase_last drops it - reported after the new
    //record so that both backends emit put, erase, pointer in the same order
    std::optional<RecordKey> erased_rev;
    std::string erased_key;
    if (x.mode != UpdateLastRevision::disable) {
        auto &currev = _storage[x.variable];
        //the width check is what makes decoding safe - a pointer of any other length is
        //not a RecordKey, the same guard MemStorage::get applies before using one
        if (x.mode == UpdateLastRevision::enable_erase_last
                && currev.size() == recordkey_string_size) {
            erased_rev = string_to_record_key(currev);
            erased_key = wholeKey(x.variable, currev);
            _storage.erase(erased_key);
        }
        currev = tmp;
    }
    std::string key = wholeKey(x.variable, tmp);
    auto &ref = _storage[key] = std::move(x.data);
    _watcher(ReplicatorEvent{.type = Type::put_key_value, .name = x.variable,
                             .recordkey = x.key, .value = ref});
    if (erased_rev) {
        _watcher(ReplicatorEvent{.type = Type::erase_key, .name = x.variable,
                                 .recordkey = *erased_rev});
    }
    if (x.mode != UpdateLastRevision::disable) {
        _watcher(ReplicatorEvent{.type = Type::update_latest, .name = x.variable,
                                 .recordkey = x.key});
    }
}
inline void MemStorage::apply(MemStorageTransaction::OpPutSchema &&x) {
    using Type = ReplicatorEvent::Type;
    auto &ref = _schemas[x.hash] = std::move(x.schema);
    _watcher(ReplicatorEvent{.type = Type::put_schema, .value = ref, .schema_hash = x.hash});
}


inline void MemStorage::apply(MemStorageTransaction::OpReplicate &&x) {
    using Type = ReplicatorEvent::Type;
    //re-emitted from the stored copies, never from moved-from members, so that
    //cascaded replication (A -> B -> C) forwards the real content
    switch (x.type) {
        case Type::put_schema: {
            auto &ref = _schemas[x.schema_hash] = std::move(x.value);
            _watcher(ReplicatorEvent{.type = Type::put_schema, .value = ref,
                                     .schema_hash = x.schema_hash});
        } break;
        case Type::put_key_value: {
            std::string key = wholeKey(x.name, x.recordkey);
            auto &ref = _storage[key] = std::move(x.value);
            _watcher(ReplicatorEvent{.type = Type::put_key_value, .name = x.name,
                                     .recordkey = x.recordkey, .value = ref});
        } break;
        case Type::update_latest: {
            auto rk = record_key_to_string(x.recordkey);
            _storage[x.name] = rk;
            _watcher(ReplicatorEvent{.type = Type::update_latest, .name = x.name,
                                     .recordkey = x.recordkey});
        } break;
        case Type::erase_key: {
            std::string key = wholeKey(x.name, x.recordkey);
            _storage.erase(key);
            _watcher(ReplicatorEvent{.type = Type::erase_key, .name = x.name,
                                     .recordkey = x.recordkey});
        } break;
        case Type::erase_latest:
            _storage.erase(x.name);
            _watcher(ReplicatorEvent{.type = Type::erase_latest, .name = x.name});
            break;
        case Type::erase_name:
            apply(MemStorageTransaction::OpErase{std::move(x.name)});
            break;
    }
}

inline void MemStorageTransaction::apply(const IStorage::ReplicatorEvent &event) {
    _ops.push_back(OpReplicate{event.type, std::string(event.name), event.recordkey,
                               std::string(event.value), event.schema_hash});
}

}