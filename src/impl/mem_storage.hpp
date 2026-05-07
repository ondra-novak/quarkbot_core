#pragma once

#include "ifc/defs.hpp"
#include "ifc/storage.hpp"
#include "ifc/types.hpp"
#include "utils/bigendian.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <iterator>
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
    virtual void erase(std::string_view variable_name) override;
    virtual void erase(std::string_view variable_name, const RecordKey &key) override;
    virtual void put_schema_binary(SchemaHash hash, std::string_view binary) override;


private:
    struct OpPut   { std::string variable; RecordKey key; std::string data; bool update ;};
    struct OpErase { std::string variable; };
    struct OpEraseRev { std::string variable; RecordKey rev; };
    struct OpPutSchema { SchemaHash hash; std::string schema; };
    using Op = std::variant<OpPut, OpErase, OpEraseRev, OpPutSchema>;

    std::vector<Op> _ops;
    std::shared_ptr<MemStorage> _storage;

    friend class MemStorage;
};

class MemStorage final : public IStorage, public std::enable_shared_from_this<MemStorage> {
    friend class MemStorageTransaction;
public:
        virtual Value get(std::string_view variable_name) const override;
        virtual Value get(std::string_view variable_name, const RecordKey &key) const override;
        virtual Enumerator get_enumerator(std::string_view variable_name, const RecordKey &since, const RecordKey &until) const override;
        virtual std::vector<std::string> list(std::string_view prefix ) const override;
        virtual Value get_schema_binary(SchemaHash h) const override;
        virtual PStorageTransaction write() override;
        virtual void add_precommit_hook_connection(WatcherSlot::Connection consumer) override {
            connect(_watcher, consumer);
        }

        WatcherSlot &get_watcher() {return _watcher;}

    static PStorage create() {
        return std::make_shared<MemStorage>();
    }
    

private:    
    std::map<std::string, std::string, std::less<> > _storage;
    std::map<SchemaHash, std::string> _schemas;
    WatcherSlot _watcher;

    void apply(const MemStorageTransaction::OpErase &x);
    void apply(const MemStorageTransaction::OpEraseRev &x);
    void apply(const MemStorageTransaction::OpPut &x);
    void apply(const MemStorageTransaction::OpPutSchema &x);

    static std::string wholeKey(std::string_view variable_name, const RecordKey &key);
    static std::string record_key_to_string(const RecordKey &key);
    static RecordKey string_to_record_key(std::string_view str);
    static std::uint64_t random_key_counter ;

};

inline  std::uint64_t MemStorage::random_key_counter ;

inline PStorage MemStorageTransaction::get_storage() const  {
    return _storage;
}
inline void MemStorageTransaction::commit(bool) {
    for (const auto &x: _ops) {
        std::visit([&](const auto &x) {
            _storage->apply(x);
        }, x);
    }
}


inline RecordKey MemStorageTransaction::put(std::string_view variable_name, std::string_view content) {
    RecordKey rc { static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()), MemStorage::random_key_counter++};
    put(variable_name, rc, content, UpdateLastRevision::enable);
    return rc;

}
inline void MemStorageTransaction::put(std::string_view variable_name, const RecordKey &key, std::string_view content, UpdateLastRevision update) {
    _ops.push_back(OpPut{std::string(variable_name), key, std::string(content), 
            update == UpdateLastRevision::enable});
    _storage->get_watcher()(*this, variable_name, key, content);

}
inline void MemStorageTransaction::erase(std::string_view variable_name) {
    _ops.push_back(OpErase{std::string(variable_name)});
    _storage->get_watcher()(*this, variable_name, RecordKey::min(), std::nullopt);
}
inline void MemStorageTransaction::erase(std::string_view variable_name, const RecordKey &key) {
    _ops.push_back(OpEraseRev{std::string(variable_name), key});
    _storage->get_watcher()(*this, variable_name, key, std::nullopt);
}
inline void MemStorageTransaction::put_schema_binary(SchemaHash hash, std::string_view binary) {
    _ops.push_back(OpPutSchema{hash, std::string(binary)});
}

inline MemStorage::Value MemStorage::get(std::string_view variable_name) const {
    auto iter = _storage.find(variable_name);
    if (iter == _storage.end() || iter->second.size() != sizeof(RecordKey)) return {{},{},false,{}};
    std::array<char, sizeof(RecordKey)> buff;
    std::copy(iter->second.begin(), iter->second.end(), buff.begin());
    return MemStorage::get(variable_name, std::bit_cast<RecordKey>(buff));
    
}

inline  std::string MemStorage::wholeKey(std::string_view variable_name, const RecordKey &key) {
    std::string whole_key;
    whole_key.resize(variable_name.size()+sizeof(RecordKey)+1);
    auto iter = std::copy(variable_name.begin(), variable_name.end(), whole_key.begin());
    *iter++ = '\0';
    auto bin = record_key_to_string(key);
    std::copy(bin.begin(), bin.end(), iter);
    return whole_key;

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
inline MemStorage::Enumerator MemStorage::get_enumerator(std::string_view variable_name, const RecordKey &since, const RecordKey &until) const  {
    auto beg = wholeKey(variable_name, since);
    auto end = wholeKey(variable_name, until);
    // +1 skips the '\0' separator between variable_name and the big-endian RecordKey bytes
    auto sz = variable_name.size() + 1;

    constexpr auto load = [](ValueView &w, auto iter, std::size_t sz) {
            w.key = string_to_record_key(std::string_view(iter->first).substr(sz));
            w.data = iter->second;
    };

    if (beg < end) {
        auto iter = _storage.lower_bound(beg);
        auto end_iter = _storage.lower_bound(end);
        return [load, iter, end_iter, sz](ValueView &w)mutable -> bool {
            if (iter == end_iter) return false;
            load(w, iter, sz);
            ++iter;
            return true;
        };
    } else if (beg > end) {
        auto iter(_storage.lower_bound(beg));
        auto end_iter(_storage.lower_bound(end));
        return [load, iter, end_iter, sz](ValueView &w) mutable -> bool {
            if (iter == end_iter) return false;
            load(w, iter, sz);
            --iter;
            return true;
        };
    } else {
        return [](ValueView &){return false;};
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
inline MemStorage::Value MemStorage::get_schema_binary(SchemaHash h) const {
    auto iter = _schemas.find(h);
    if (iter == _schemas.end()) {
        return {{},{},false,{}};
    }
    return {{}, iter->second, true, {}};
}
inline PStorageTransaction MemStorage::write() {
    return std::make_unique<MemStorageTransaction>(shared_from_this());
}

inline void MemStorage::apply(const MemStorageTransaction::OpErase &x) {
    _storage.erase(x.variable);
    std::string beg = x.variable + '\0';
    std::string end = x.variable + '\x01';
    auto beg_iter = _storage.lower_bound(beg);
    auto end_iter = _storage.lower_bound(end);
    auto iter = beg_iter;
    while (iter != end_iter ) {
        iter = _storage.erase(iter);
    }
}
inline void MemStorage::apply(const MemStorageTransaction::OpEraseRev &x) {
    _storage.erase(wholeKey(x.variable, x.rev));
}
inline void MemStorage::apply(const MemStorageTransaction::OpPut &x) {
    auto tmp = std::bit_cast<std::array<char, sizeof(RecordKey)> >(x.key);
    if (x.update) _storage[x.variable] = std::string(tmp.begin(), tmp.end());
    _storage[wholeKey(x.variable, x.key)] = x.data;
}
inline void MemStorage::apply(const MemStorageTransaction::OpPutSchema &x) {
    _schemas[x.hash] = x.schema;
}

inline std::string MemStorage::record_key_to_string(const RecordKey &key) {
    std::string out;
    auto iter = std::back_inserter(out);
    big_endian_binarize(key.ordered, iter);
    big_endian_binarize(key.random, iter);
    return out;
}

inline RecordKey MemStorage::string_to_record_key(std::string_view str) {
    RecordKey key;
    auto iter = str.begin();
    big_endian_unbinarize(key.ordered, iter);
    big_endian_unbinarize(key.random, iter);
    return key;
}


}