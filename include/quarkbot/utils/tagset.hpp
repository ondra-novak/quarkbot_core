#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>


class TagSet {
public:


    using DomainBase = std::span<const std::string_view>;

    template<unsigned int N>
    struct Domain: public DomainBase {
        using Super = std::span<const std::string_view>;
        std::string_view _items[N] = {};

        constexpr Domain(const std::string_view(&&items)[N]):Super(_items) {
            for (unsigned int i = 0; i < N; ++i) _items[i] = items[i];
        }
        constexpr Domain(const std::string_view(&items)[N]):Super(_items) {
            for (unsigned int i = 0; i < N; ++i) _items[i] = items[i];
        }
        constexpr Domain(const Domain &) = delete;
        constexpr Domain &operator=(const Domain &) = delete;
    };

    TagSet() = default;
    constexpr TagSet(const DomainBase &domain, std::uint64_t mask):_bitvector(mask),_mapping(&domain) {}
    constexpr TagSet(std::uint64_t mask, const TagSet &domain): _bitvector(mask), _mapping(domain._mapping) {}
    constexpr TagSet(std::string_view tag, const TagSet &domain):_bitvector(0), _mapping(domain._mapping) {
        _bitvector = get_raw(tag);
    }
    template<typename Iter>
    constexpr Iter to_string(Iter target, std::string_view separator = ",") {
        auto c = _bitvector;
        std::size_t pos = 0;
        bool sep = false;
        while (c) {
            if (c & 1) {
                if (sep) std::copy(separator.begin(), separator.end(), target);
                std::copy((*_mapping)[pos].begin(), (*_mapping)[pos].end(), target);
                sep = true;
            }
            c>>=1;
            pos++;
        }
        return target;
    }
    constexpr static TagSet from_string(const DomainBase &domain, std::string_view content, std::string_view sep = ",") {
        TagSet res(domain, 0);
        auto p = content.find(sep);
        while (p != content.npos) {
            auto sub = sep.substr(0,p);
            content.remove_prefix(p + sep.size());
            res |= sub;
        }
        res |= content;
        return res;
    }


    constexpr std::string to_string(std::string_view separator = ",") {
        std::string out;
        to_string(std::back_inserter(out), separator);
        return out;
    }

    constexpr std::size_t get_raw(const std::string_view &tag) const {
        auto iter = std::find(_mapping->begin(), _mapping->end(), tag);
        if (iter != _mapping->end())  {
            return static_cast<std::size_t>(1<<std::distance(_mapping->begin(), iter));
        }
        return 0;
    }

    constexpr bool contains(const TagSet &x) {
        if (x._mapping != _mapping) return false;
        return (x._bitvector & _bitvector) == _bitvector;
    }
    constexpr bool contains(const std::string_view &x) {
        return (_bitvector & get_raw(x)) != 0;
    }
    constexpr bool contains_one_of(const TagSet &x) {
        if (x._mapping != _mapping) return false;
        return (x._bitvector & _bitvector) != 0;
    }

    constexpr bool operator==(const TagSet &) const = default;

    constexpr TagSet operator|(const TagSet &other) const {
        assert(_mapping == other._mapping);        
        return {_bitvector | other._bitvector, *this};
    }
    constexpr TagSet operator|(const std::string_view &other) const {
        return {_bitvector | get_raw(other), *this};
    }
    friend constexpr TagSet operator|(const std::string_view &other, const TagSet &me)  {
        return me | other;
    }

    constexpr TagSet operator&(const TagSet &other) const {
        if (_mapping == other._mapping) return {_bitvector & other._bitvector, *this};
        return {0,*this};
    }
    constexpr TagSet &operator|=(const TagSet &other)  {
        assert(_mapping == other._mapping);        
        _bitvector |= other._bitvector;
        return *this;
    }
    constexpr TagSet &operator|=(const std::string_view &other)  {
        _bitvector |= get_raw(other);
        return *this;
    }   

    constexpr TagSet &operator&=(const TagSet &other)  {
        if (_mapping == other._mapping) {
            _bitvector &= other._bitvector;
        } else {
            _bitvector = 0;
        }
        return *this;
    }
    constexpr std::uint64_t get_raw() const {return _bitvector;}


protected:
    std::uint64_t _bitvector = 0;
    const DomainBase *_mapping = nullptr;
};

