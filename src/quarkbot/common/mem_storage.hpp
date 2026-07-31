#pragma once

#include "quarkbot/storage.hpp"
#include "quarkbot/types.hpp"
#include "quarkbot/utils/bigendian.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <concepts>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

namespace quarkbot {


template<typename T>
concept MemStorageConfig = requires() {
    {T::keep_history}->std::convertible_to<bool>;
};

struct MemStorageDefaultConfig {
    ///Default behaviour, full operation
    static constexpr auto keep_history = true;
};

struct MemStorageBacktestConfig {
    ///Will not store history for puts with revisions, revision is set to zero
    static constexpr auto keep_history = false;
};

template<MemStorageConfig Config>
class MemStorage;


template<MemStorageConfig Config = MemStorageDefaultConfig>
class MemStorageTransaction final : public IStorageTransaction {
public:
    explicit MemStorageTransaction(std::shared_ptr<MemStorage<Config> > storage) : _storage(storage) {}

            ///retrieve associated storage of this transaction
    virtual PStorage get_storage() const  override;
    virtual void commit(bool sync) override;
    virtual RecordKey put(std::string_view variable_name, std::string_view content) override;
    virtual void put(std::string_view variable_name, const RecordKey &key, std::string_view content,
        UpdateLastRevision update) override;
    virtual void erase(std::string_view variable_name) override;
    virtual void erase(std::string_view variable_name, const RecordKey &key) override;
    virtual void put_schema_binary(srl::SchemaHash hash, std::string_view binary) override;


private:
    struct OpPut   { std::string variable; RecordKey key; std::string data; bool update ;};
    struct OpErase { std::string variable; };
    struct OpEraseRev { std::string variable; RecordKey rev; };
    struct OpPutSchema { srl::SchemaHash hash; std::string schema; };
    using Op = std::variant<OpPut, OpErase, OpEraseRev, OpPutSchema>;

    std::vector<Op> _ops;
    std::shared_ptr<MemStorage<Config> > _storage;

    friend class MemStorage<Config>;
};

template<MemStorageConfig Config = MemStorageDefaultConfig>
class MemStorage final : public IStorage, public std::enable_shared_from_this<MemStorage<Config> > {
    friend class MemStorageTransaction<Config>;
public:

        virtual Value get(std::string_view variable_name) const override;
        virtual Value get(std::string_view variable_name, const RecordKey &key) const override;
        virtual Enumerator get_enumerator(std::string_view variable_name, const RecordKey &since, const RecordKey &until) const override;
        virtual std::vector<std::string> list(std::string_view prefix ) const override;
        virtual Value get_schema_binary(srl::SchemaHash h) const override;
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
    std::map<srl::SchemaHash, std::string> _schemas;
    WatcherSlot _watcher;
    bool _keep_history;

    void apply(const typename MemStorageTransaction<Config>::OpErase &x);
    void apply(const typename MemStorageTransaction<Config>::OpEraseRev &x);
    void apply(const typename MemStorageTransaction<Config>::OpPut &x);
    void apply(const typename MemStorageTransaction<Config>::OpPutSchema &x);

    static std::string wholeKey(std::string_view variable_name, const RecordKey &key);
    static std::string record_key_to_string(const RecordKey &key);
    static RecordKey string_to_record_key(std::string_view str);
    static std::uint64_t random_key_counter ;

};

template<MemStorageConfig Config>
inline  std::uint64_t MemStorage<Config>::random_key_counter ;

template<MemStorageConfig Config>
inline PStorage MemStorageTransaction<Config>::get_storage() const  {
    return _storage;
}
template<MemStorageConfig Config>
inline void MemStorageTransaction<Config>::commit(bool) {
    for (const auto &x: _ops) {
        std::visit([&](const auto &x) {
            _storage->apply(x);
        }, x);
    }
}


template<MemStorageConfig Config>
inline RecordKey MemStorageTransaction<Config>::put(std::string_view variable_name, std::string_view content) {
    RecordKey rc { static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()), MemStorage<Config>::random_key_counter++};
    put(variable_name, rc, content, UpdateLastRevision::enable);
    return rc;

}
template<MemStorageConfig Config>
inline void MemStorageTransaction<Config>::put(std::string_view variable_name, const RecordKey &key, std::string_view content, UpdateLastRevision update) {
    _ops.push_back(OpPut{std::string(variable_name), key, std::string(content), 
            update == UpdateLastRevision::enable});
    _storage->get_watcher()(*this, variable_name, key, content);

}
template<MemStorageConfig Config>
inline void MemStorageTransaction<Config>::erase(std::string_view variable_name) {
    _ops.push_back(OpErase{std::string(variable_name)});
    _storage->get_watcher()(*this, variable_name, RecordKey::min(), std::nullopt);
}
template<MemStorageConfig Config>
inline void MemStorageTransaction<Config>::erase(std::string_view variable_name, const RecordKey &key) {
    _ops.push_back(OpEraseRev{std::string(variable_name), key});
    _storage->get_watcher()(*this, variable_name, key, std::nullopt);
}
template<MemStorageConfig Config>
inline void MemStorageTransaction<Config>::put_schema_binary(srl::SchemaHash hash, std::string_view binary) {
    _ops.push_back(OpPutSchema{hash, std::string(binary)});
}

template<MemStorageConfig Config>
inline MemStorage<Config>::Value MemStorage<Config>::get(std::string_view variable_name) const {    
    if constexpr (Config::keep_history) {
        auto iter = _storage.find(variable_name);
        if (iter == _storage.end() || iter->second.size() != sizeof(RecordKey)) return {{},{},false,{}};
        std::array<char, sizeof(RecordKey)> buff;
        std::copy(iter->second.begin(), iter->second.end(), buff.begin());
        return MemStorage<Config>::get(variable_name, std::bit_cast<RecordKey>(buff));
    } else {
        return MemStorage<Config>::get(variable_name, {});
    }
    
}

template<MemStorageConfig Config>
inline  std::string MemStorage<Config>::wholeKey(std::string_view variable_name, const RecordKey &key) {
    std::string whole_key;
    whole_key.resize(variable_name.size()+sizeof(RecordKey)+1);
    auto iter = std::copy(variable_name.begin(), variable_name.end(), whole_key.begin());
    *iter++ = '\0';
    auto bin = record_key_to_string(key);
    std::copy(bin.begin(), bin.end(), iter);
    return whole_key;

}

template<MemStorageConfig Config>
inline typename MemStorage<Config>::Value MemStorage<Config>::get(std::string_view variable_name, const RecordKey &key) const {
    Value out = { {}, {}, {} , key};
    auto iter = _storage.find(wholeKey(variable_name, key));
    if (iter != _storage.end()) {
        out.data = iter->second;
        out.exists = true;
    }
    return out;


}
template<MemStorageConfig Config>
inline typename MemStorage<Config>::Enumerator MemStorage<Config>::get_enumerator(std::string_view variable_name, const RecordKey &since, const RecordKey &until) const  {
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
template<MemStorageConfig Config>
inline std::vector<std::string> MemStorage<Config>::list(std::string_view prefix ) const {
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
template<MemStorageConfig Config>
inline typename MemStorage<Config>::Value MemStorage<Config>::get_schema_binary(srl::SchemaHash h) const {
    auto iter = _schemas.find(h);
    if (iter == _schemas.end()) {
        return {{},{},false,{}};
    }
    return {{}, iter->second, true, {}};
}
template<MemStorageConfig Config>
inline PStorageTransaction MemStorage<Config>::write() {
    return std::make_unique<MemStorageTransaction<Config> >(this->shared_from_this());
}

template<MemStorageConfig Config>
inline void MemStorage<Config>::apply(const MemStorageTransaction<Config>::OpErase &x) {
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
template<MemStorageConfig Config>
inline void MemStorage<Config>::apply(const MemStorageTransaction<Config>::OpEraseRev &x) {
    _storage.erase(wholeKey(x.variable, x.rev));
}
template<MemStorageConfig Config>
inline void MemStorage<Config>::apply(const MemStorageTransaction<Config>::OpPut &x) {
    if constexpr(Config::keep_history) {
        auto tmp = std::bit_cast<std::array<char, sizeof(RecordKey)> >(x.key);
        if (x.update) _storage[x.variable] = std::string(tmp.begin(), tmp.end());
        _storage[wholeKey(x.variable, x.key)] = x.data;
    } else {
        _storage[wholeKey(x.variable, RecordKey{})] = x.data;
    }
}
template<MemStorageConfig Config>
inline void MemStorage<Config>::apply(const MemStorageTransaction<Config>::OpPutSchema &x) {
    _schemas[x.hash] = x.schema;
}

template<MemStorageConfig Config>
inline std::string MemStorage<Config>::record_key_to_string(const RecordKey &key) {
    std::string out;
    auto iter = std::back_inserter(out);
    big_endian_binarize(key.ordered, iter);
    big_endian_binarize(key.random, iter);
    return out;
}

template<MemStorageConfig Config>
inline RecordKey MemStorage<Config>::string_to_record_key(std::string_view str) {
    RecordKey key;
    auto iter = str.begin();
    big_endian_unbinarize(key.ordered, iter);
    big_endian_unbinarize(key.random, iter);
    return key;
}


}