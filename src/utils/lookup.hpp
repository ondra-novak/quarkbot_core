#pragma once

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
template<typename Column1, typename Column2, std::size_t count>
class LookupTable {
public:

    using Pair = std::pair<Column1, Column2>;
    

    constexpr LookupTable(std::span<const Pair> data) {
        std::copy(data.begin(), data.end(), this->data.begin());
        if constexpr(std::totally_ordered<Column1>) {
            std::sort(this->data.begin(), this->data.end(), CompareOp());
        }
    }

    constexpr std::optional<Column1> operator()(const Column2 &v) const {
        auto iter = std::find_if(data.begin(), data.end(), [&](const Pair &p) {
            return p.second == v;
        });
        return iter == data.end()?std::optional<Column1>(std::nullopt):std::optional<Column1>(iter->first);
    }
    constexpr std::optional<Column2> operator()(const Column1 &v) const {
        decltype(data.begin()) iter;
        if constexpr(std::totally_ordered<Column1>) {
            iter = std::lower_bound(data.begin(), data.end(), std::pair<const Column1, std::nullptr_t>(v, {}), CompareOp());
            if (iter != data.end() && iter->first != v) iter = data.end();
        } else  {
            iter = std::find_if(data.begin(), data.end(), [&](const Pair &p) {
            return p.first == v;
            });
        }
        return iter == data.end()?std::optional<Column2>(std::nullopt):std::optional<Column2>(iter->second);        
    }

    constexpr auto begin() const {return data.begin();}
    constexpr auto end() const {return data.end();}
    

protected:

    std::array<Pair, count> data = {};

    struct CompareOp {
    template<typename _Pair1, typename _Pair2>
        constexpr static bool operator()(const _Pair1 &a, const _Pair2 &b) {
                return a.first < b.first;        
        }
    };
};

template<typename A, typename B, std::size_t N>
constexpr LookupTable<A, B, N> make_lookup_table(const std::pair<A, B> (&table)[N]) {
    return {table};
}
template<typename A, std::size_t N>
constexpr LookupTable<A, std::string_view, N> make_string_lookup_table(const std::pair<A, std::string_view> (&table)[N]) {
    return {table};
}

template<typename A, std::size_t N>
constexpr std::string lookup_available_options(const LookupTable<A, std::string_view, N> &src) {
    std::string out;
    auto b = src.begin();
    auto e = src.end();
    if (b != e) {
        out.append(b->second);
        ++b;
        while (b != e) {
            out.append(", ");
            out.append(b->second);
            ++b;
        }
    }
    return out;
}
