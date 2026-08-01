#pragma once

#include "quarkbot/abstract/iserie.hpp"
#include "storage.hpp"
#include "types.hpp"
#include <bit>
#include <memory>
#include <optional>
#include <type_traits>
#include "persistent.hpp"

namespace quarkbot {

template<typename T, CommitStrategy cs = CommitStrategy::delayed>
class PersistentSerie;

template<typename T, CommitStrategy cs>
requires (std::is_trivially_copyable_v<T>)
class PersistentSerie<T,cs> final: public ISerie<T> {
public:

    using value_type = T;

    PersistentSerie(Storage storage, std::string key, std::size_t size = 0, std::uint64_t section = 0):_storage(std::move(storage)),_key(std::move(key)), _size(size) {
        auto val = _storage.get(_key);
        if (val.exists) {
            _rev = val.key;
            _rev.random = section;
        } else {
            _rev = {0,section};
        }
    }

    PersistentSerie(const PersistentNamespace &ns, std::string_view subkey, std::size_t size = 0, std::uint64_t section = 0)
        :PersistentSerie(ns.get_storage(), ns.sub_ns(subkey).get_name(), size, section) {}


    virtual void reserve(std::size_t sz) override {
        this->_size = std::max<std::size_t>(this->_size,sz);
    }

    virtual void put(T value) override {
        auto binary = std::bit_cast<std::array<char, sizeof(T)> >(value);
        ++_rev.ordered;

        if constexpr(cs == CommitStrategy::delayed) {
            auto &trn = shared_transaction(_storage);
            trn.put(_key, _rev, std::string_view(binary.data(), binary.size()));
            if (_size) trn.erase(_key, {_rev.ordered-_size, _rev.random});
        } else {
            auto trn = _storage.write();
            trn.put(_key, _rev, std::string_view(binary.data(), binary.size()));
            if (_size) trn.erase(_key, {_rev.ordered-_size, _rev.random});
            trn.commit(cs == CommitStrategy::immediately_sync);
        }
    }

    virtual std::optional<T> operator[](std::size_t index) const override {
        RecordKey rc{_rev.ordered-index, _rev.random};
        auto val = _storage.get(_key, rc);
        std::optional<T> out;
        if (val.exists) {
            std::array<char, sizeof(T)> buff;
            std::copy(val.data.begin(), val.data.end(), buff.begin());
            out.emplace(std::bit_cast<T>(buff));
        }
        return out;
    }

    PersistentSerie clone() const {
        return PersistentSerie(_storage, _key, _size, _rev.random+1);
    }

    virtual std::shared_ptr<ISerie<T> > clone_ptr() const override {
        return std::make_shared<PersistentSerie>(clone());
    }

protected:
    Storage _storage;
    std::string _key;
    std::size_t _size;
    RecordKey _rev;
};

static_assert(IsSerie<PersistentSerie<int> >);



}