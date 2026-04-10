#pragma once

#include "ifc/storage.hpp"
#include <map>
#include <memory>
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
    void prune_history(Key key, Revision from, Revision to) override;
    void commit() override;

private:
    struct OpPut   { std::string key_name; bool sequence; std::string data; };
    struct OpErase { std::string key_name; bool sequence; };
    struct OpPrune { std::string key_name; bool sequence; Revision from, to; };
    using Op = std::variant<OpPut, OpErase, OpPrune>;

    std::vector<Op> _ops;
    MemStorage &_storage;
};

class MemStorage final : public IStorage {
public:
    Value get(Key key) const override;
    Value get(Key key, Revision rev) const override;
    std::vector<std::string> get_all_keys(const Key &filter) const override;
    PStorageTransaction write() override;

    Revision apply_put(Key key, std::string_view data);
    Revision apply_erase(Key key);
    void apply_prune(Key key, Revision from, Revision to);
    Revision next_seq_rev(std::string_view name) const;

private:
    struct SeqEntry {
        Revision next_rev = 1;
        std::map<Revision, std::pair<bool, std::string>> history;
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
    auto last = entry.history.rbegin();
    return {last->first, last->second.first, last->second.second};
}
inline IStorage::Value MemStorage::get(Key key, Revision rev) const {
    if (!key.sequence) return get(key);
    auto it = _seq.find(std::string(key.name));
    if (it == _seq.end()) return {0, false, {}};
    const auto &entry = it->second;
    auto hit = entry.history.find(rev);
    if (hit == entry.history.end()) return {0, false, {}};
    return {rev, hit->second.first, hit->second.second};
}
inline std::vector<std::string> MemStorage::get_all_keys(const Key &filter) const {
    std::vector<std::string> result;
    if (!filter.sequence) {
        for (const auto &[k, v] : _plain) {
            if (k.starts_with(filter.name)) result.push_back(k);
        }
    } else {
        for (const auto &[k, e] : _seq) {
            if (!k.starts_with(filter.name)) continue;
            if (e.history.empty()) continue;
            if (e.history.rbegin()->second.first)  // latest revision is live (exists=true)
                result.push_back(k);
        }
    }
    return result;
}
inline PStorageTransaction MemStorage::write() { return std::make_unique<MemStorageTransaction>(*this); }
inline IStorage::Revision MemStorage::apply_put(Key key, std::string_view data) {
    if (!key.sequence) {
        _plain[std::string(key.name)] = std::string(data);
        return 0;
    }
    auto &entry = _seq[std::string(key.name)];
    Revision rev = entry.next_rev++;
    entry.history.emplace(rev, std::make_pair(true, std::string(data)));
    return rev;
}
inline IStorage::Revision MemStorage::apply_erase(Key key) {
    if (!key.sequence) {
        _plain.erase(std::string(key.name));
        return 0;
    }
    auto it = _seq.find(std::string(key.name));
    if (it == _seq.end()) return 0;  // nothing to erase, no-op
    auto &entry = it->second;
    Revision rev = entry.next_rev++;
    entry.history.emplace(rev, std::make_pair(false, std::string{}));
    return rev;
}
inline void MemStorage::apply_prune(Key /*key*/, Revision /*from*/, Revision /*to*/) {}
inline IStorage::Revision MemStorage::next_seq_rev(std::string_view name) const {
    auto it = _seq.find(std::string(name));
    if (it == _seq.end()) return 1;
    return it->second.next_rev;
}

// --- MemStorageTransaction ---

inline IStorageTransaction::Revision MemStorageTransaction::put(Key key, std::string_view value_blob) {
    _ops.emplace_back(OpPut{std::string(key.name), key.sequence, std::string(value_blob)});
    return key.sequence ? _storage.next_seq_rev(key.name) : 0;
}
inline IStorageTransaction::Revision MemStorageTransaction::erase(Key key) {
    _ops.emplace_back(OpErase{std::string(key.name), key.sequence});
    return key.sequence ? _storage.next_seq_rev(key.name) : 0;
}
inline void MemStorageTransaction::prune_history(Key key, Revision from, Revision to) {
    _ops.emplace_back(OpPrune{std::string(key.name), key.sequence, from, to});
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
                _storage.apply_prune({o.key_name, o.sequence}, o.from, o.to);
        }, op);
    }
    _ops.clear();
}

} // namespace quarkbot
