#pragma once

#include "quarkbot/abstract/istorage.hpp"
#include "quarkbot/common/storage_common.hpp"
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
    virtual void commit(bool sync) override;
    virtual RecordKey put(std::string_view variable_name, std::string_view content) override;
    virtual void put(std::string_view variable_name, const RecordKey &key, std::string_view content,
        UpdateLastRevision update) override;
    virtual void put(const IStorage::ReplicatorEvent &event) override;
    virtual void erase(std::string_view variable_name) override;
    virtual void erase(std::string_view variable_name, const RecordKey &key) override;
    virtual void put_schema_binary(srl::SchemaHash hash, std::string_view binary) override;


private:
    struct OpPut   { std::string variable; RecordKey key; std::string data; UpdateLastRevision mode ;};
    struct OpErase { std::string variable; };
    struct OpReplicate { std::string key; std::string value ; bool erase; IStorage::ReplicatorEvent::Kind kind;};
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
        virtual PStorageTransaction write() override;
        virtual void add_replicator(Replicator::Connection consumer) override {
            connect(_watcher, consumer);
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
inline void MemStorageTransaction::commit(bool) {
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
inline PStorageTransaction MemStorage::write() {
    return std::make_unique<MemStorageTransaction>(shared_from_this());
}

inline void MemStorage::apply(MemStorageTransaction::OpErase &&x) {
    _storage.erase(x.variable);
    _watcher(ReplicatorEvent{x.variable,{}, true});
    std::string beg = x.variable + '\0';
    std::string end = x.variable + '\x01';
    auto beg_iter = _storage.lower_bound(beg);
    auto end_iter = _storage.lower_bound(end);
    auto iter = beg_iter;
    while (iter != end_iter ) {
    _watcher(ReplicatorEvent{iter->first,{}, true});
        iter = _storage.erase(iter);
    }
}
inline void MemStorage::apply(MemStorageTransaction::OpEraseRev &&x) {
    std::string key = wholeKey(x.variable, x.rev);
    _storage.erase(key);
    _watcher(ReplicatorEvent{key,{}, true});

}
inline void MemStorage::apply(MemStorageTransaction::OpPut &&x) {
    auto tmp = record_key_to_string(x.key);
    if (x.mode != UpdateLastRevision::disable) {
        auto &currev = _storage[x.variable];
        if (x.mode == UpdateLastRevision::enable_erase_last && !currev.empty()) {
            std::string key = wholeKey(x.variable, currev);
            _storage.erase(key);
            _watcher(ReplicatorEvent{key,{}, true});
        }
        currev = tmp;
        _watcher(ReplicatorEvent{x.variable,tmp, false});
    }
    std::string key = wholeKey(x.variable, tmp);    
    auto &ref =_storage[key] = std::move(x.data);
    _watcher(ReplicatorEvent{key, ref, false});
}
///the logical key of a schema record is the binary SchemaHash
inline std::array<char, sizeof(srl::SchemaHash)> schema_hash_to_key(srl::SchemaHash h) {
    return std::bit_cast<std::array<char, sizeof(srl::SchemaHash)> >(h);
}

inline std::optional<srl::SchemaHash> schema_key_to_hash(std::string_view key) {
    if (key.size() != sizeof(srl::SchemaHash)) return {};
    std::array<char, sizeof(srl::SchemaHash)> bin;
    std::copy(key.begin(), key.end(), bin.begin());
    return std::bit_cast<srl::SchemaHash>(bin);
}

inline void MemStorage::apply(MemStorageTransaction::OpPutSchema &&x) {
    auto &ref = _schemas[x.hash] = std::move(x.schema);
    auto key = schema_hash_to_key(x.hash);
    _watcher(ReplicatorEvent{{key.data(), key.size()}, ref, false, ReplicatorEvent::Kind::schema});
}


inline void MemStorage::apply(MemStorageTransaction::OpReplicate &&x) {
    //the event is re-emitted from the stored copies, never from moved-from members,
    //so that cascaded replication (A -> B -> C) forwards the real content
    if (x.kind == ReplicatorEvent::Kind::schema) {
        auto h = schema_key_to_hash(x.key);
        if (!h) return;     //malformed schema key - nothing sensible to store
        if (x.erase) {
            _schemas.erase(*h);
            _watcher(ReplicatorEvent{x.key, {}, true, x.kind});
        } else {
            auto &ref = _schemas[*h] = std::move(x.value);
            _watcher(ReplicatorEvent{x.key, ref, false, x.kind});
        }
    } else if (x.erase) {
        _storage.erase(x.key);
        _watcher(ReplicatorEvent{x.key, {}, true, x.kind});
    } else {
        auto [iter, ins] = _storage.insert_or_assign(std::move(x.key), std::move(x.value));
        _watcher(ReplicatorEvent{iter->first, iter->second, false, x.kind});
    }
}

inline void MemStorageTransaction::put(const IStorage::ReplicatorEvent &event) {
    _ops.push_back(OpReplicate{std::string(event.key), std::string(event.value), event.erase, event.kind});
}

}