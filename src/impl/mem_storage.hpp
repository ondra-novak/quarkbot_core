#pragma once

#include "ifc/storage.hpp"
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace quarkbot {

class MemStorage;

class MemStorageTransaction final : public IStorageTransaction {
public:
    explicit MemStorageTransaction(MemStorage &storage) : _storage(storage) {}

    Revision put(Key key, std::string_view value_blob) override;
    Revision erase(Key key) override;
    void prune_history(std::string_view key, Revision to) override;
    void commit() override;

private:
    struct OpPut   { std::string key_name; bool sequence; std::string data; };
    struct OpErase { std::string key_name; bool sequence; };
    struct OpPrune { std::string key_name;  Revision to; };
    using Op = std::variant<OpPut, OpErase, OpPrune>;

    std::vector<Op> _ops;
    MemStorage &_storage;
};

class MemStorage final : public IStorage {
    friend class MemStorageTransaction;
public:
    Value get(Key key) const override;
    Value get(Key key, Revision rev) const override;
    std::vector<std::string> list(const Key &filter) const override;
    PStorageTransaction write(bool sync) override;

private:
    Revision apply_put(const Key &key, std::string_view data);
    Revision apply_erase(const Key &key);
    void apply_prune(const std::string &key,  Revision to);
    Revision next_seq_rev(std::string_view name) const;

    struct SeqEntry {
        Revision last_rev = 0;
        std::vector<std::optional<std::string> > history;
    };

    std::unordered_map<std::string, std::string> _plain;
    std::unordered_map<std::string, SeqEntry> _seq;
};

// --- MemStorage ---

inline IStorage::Value MemStorage::get(Key key) const {
    if (!key.sequence) {
        auto it = _plain.find(std::string(key.name));
        if (it == _plain.end()) return {0, false, {}};
        return {0, true, it->second};
    }
    auto it = _seq.find(std::string(key.name));
    if (it == _seq.end() || it->second.history.empty()) return {0, false, {}};
    const auto &entry = it->second;
    if (entry.history.empty() || !entry.history.back().has_value()) {
        return {entry.last_rev,false,{}};
    } else {
        return {entry.last_rev, true, entry.history.back().value()};
    }
}

inline IStorage::Value MemStorage::get(Key key, Revision rev) const {
    if (!key.sequence) return get(key);  // revision has no meaning for non-sequence keys
    auto it = _seq.find(std::string(key.name));
    if (it == _seq.end()) return {0, false, {}};
    
    const auto &entry = it->second;
    auto minindex = entry.last_rev - entry.history.size()+1;
    if (rev < minindex) return {rev, false, {}};
    auto &val = entry.history[rev - minindex];
    if (!val) return {rev, false, {}};
    return {rev, true, *val};
}

inline std::vector<std::string> MemStorage::list(const Key &filter) const {
    std::vector<std::string> result;
    if (!filter.sequence) {
        for (const auto &[k, v] : _plain) {
            if (k.starts_with(filter.name)) result.push_back(k);
        }
    } else {
        for (const auto &[k, e] : _seq) {
            if (!k.starts_with(filter.name)) continue;
            if (e.history.empty()) continue;
            if (e.history.back()) result.push_back(k);
        }
    }
    return result;
}
inline PStorageTransaction MemStorage::write(bool) { return std::make_unique<MemStorageTransaction>(*this); }
inline IStorage::Revision MemStorage::apply_put(const Key &key, std::string_view data) {
    if (!key.sequence) {
        _plain[std::string(key.name)] = std::string(data);
        return 0;
    }
    auto &entry = _seq[std::string(key.name)];
    Revision rev = ++entry.last_rev;
    entry.history.emplace_back(data);    
    return rev;
}
inline IStorage::Revision MemStorage::apply_erase(const Key &key) {
    if (!key.sequence) {
        _plain.erase(std::string(key.name));
        return 0;
    }
    auto &entry = _seq[std::string(key.name)];
    Revision rev = ++entry.last_rev;
    entry.history.push_back(std::nullopt);
    return rev;
}
inline void MemStorage::apply_prune(const std::string &key, Revision to) {
    auto it = _seq.find(std::string(key));
    if (it == _seq.end() || it->second.history.empty()) return;
    auto &entry = it->second;
    auto minrev = entry.last_rev - entry.history.size()+1;
    if (minrev >= to) return;
    auto rm = to - minrev;
    rm = std::min(rm, entry.history.size()-1);
    auto beg = entry.history.begin();
    auto end = beg;
    std::advance(end, rm);
    entry.history.erase(beg,end);
}

inline IStorage::Revision MemStorage::next_seq_rev(std::string_view name) const {
    auto it = _seq.find(std::string(name));
    if (it == _seq.end()) return 1;
    return it->second.last_rev+1;
}

// --- MemStorageTransaction ---

inline IStorageTransaction::Revision MemStorageTransaction::put(Key key, std::string_view value_blob) {
    _ops.emplace_back(OpPut{std::string(key.name), key.sequence, std::string(value_blob)});
    // Revision estimate based on current storage state — stale if same key is put multiple times
    // in one transaction. This is by design (write-batch model); actual revision assigned on commit.
    return key.sequence ? _storage.next_seq_rev(key.name) : 0;
}
inline IStorageTransaction::Revision MemStorageTransaction::erase(Key key) {
    _ops.emplace_back(OpErase{std::string(key.name), key.sequence});
    // Same stale-estimate caveat as put() above.
    return key.sequence ? _storage.next_seq_rev(key.name) : 0;
}
inline void MemStorageTransaction::prune_history(std::string_view key, Revision to) {
    _ops.emplace_back(OpPrune{std::string(key),  to});
}
inline void MemStorageTransaction::commit() {
    for (auto &op : _ops) {
        std::visit([this](auto &o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, OpPut>)
                _storage.apply_put({o.key_name, o.sequence}, o.data);
            else if constexpr (std::is_same_v<T, OpErase>)
                _storage.apply_erase({o.key_name, o.sequence});
            else
                _storage.apply_prune(o.key_name,  o.to);
        }, op);
    }
    _ops.clear();
}

} // namespace quarkbot
