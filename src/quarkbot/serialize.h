#pragma once

#include "common.h"

namespace quarkbot {

template<typename T>
concept custom_serialize = requires(T c, const std::string &s) {
    {custom_to_binary(c)}->std::convertible_to<std::string>;
    {custom_from_binary(std::in_place_type<T>,s)}->std::convertible_to<T>;
};


namespace Serializer {
    template<std::output_iterator<char> Iter, typename T>
    inline Iter to_binary(Iter iter, const T &item) {

        if constexpr(std::is_same_v<T, bool>) {
            return to_binary(iter, item?1:0);
        } else if constexpr(std::is_same_v<T,std::string_view>) {
            iter = to_binary(iter, item.size());
            return std::copy(item.begin(), item.end(), iter);
        } else if constexpr(std::is_integral_v<T>) {
            if constexpr(std::is_unsigned_v<T>) {
                T x = item;
                do {
                    *iter = static_cast<unsigned char>(x&0x7F) | ((x>=0x80)?0x80:0);
                    ++iter;
                    x>>=7;
                } while (x);
                return iter;
            } else {
                auto x = (static_cast<std::make_unsigned_t<T> >(item<0?-(item+1):item) << 1) | (item<0?1:0);
                return to_binary(iter, x);
            }
        } else if constexpr(is_container<T>) {
            auto sz = static_cast<std::size_t>(std::distance(item.begin(), item.end()));
            iter = to_binary(iter, sz);
            for (const auto &x: item) {
                iter = to_binary(iter, x);
            }
            return iter;
        } else if constexpr(std::is_trivially_copy_constructible_v<T>) {
            return std::copy(reinterpret_cast<const char *>(&item),
                             reinterpret_cast<const char *>(&item)+sizeof(item), iter);
        } else if constexpr(is_variant_type<T>) {
            iter = to_binary(iter, static_cast<unsigned int>(item.index()));
            return std::visit([&](const auto &x){return to_binary(iter, x);}, item);
        } else if constexpr(is_optional_type<T>) {
            bool hv = item.has_value();
            iter = to_binary(iter, hv);
            if (hv) iter =  to_binary(iter, *item);
            return iter;
        } else if constexpr(is_tuple_type<T>) {
            return std::apply([&](const auto & ... x){
                to_binary_args(iter, x ... );
            },item);
        } else if constexpr(custom_serialize<T>){
             return to_binary(iter, custom_to_binary(item));
        } else {
            static_assert(assert_error<T>, "This type cannot be stored in BinTuple");
            return iter;
        }
    }

    template<std::size_t idx>
    struct valid_constant {
        static constexpr std::size_t value = idx;
        static constexpr bool valid = true;
    };

    struct invalid_constant {
        static constexpr bool valid = false;
    };


    struct make_valueless_helper {
        template<typename T>
        operator T() const {throw false;}
    };

    template<std::size_t count, typename Fn,  std::size_t curidx = 0>
    auto to_constant(std::size_t idx, Fn &&fn) {
        if constexpr(curidx >= count) return fn(invalid_constant{});
        else if (curidx == idx) return fn(valid_constant<curidx>{});
        else return to_constant<count, Fn, curidx+1>(idx, std::forward<Fn>(fn));
    }

    template<typename T>
    struct tuple_hlp;

    template<typename T>
    struct type_tag {
        using type = T;
    };

    template<typename ... Args>
    struct tuple_hlp<std::tuple<Args...> > {
        template<typename Fn>
        static std::tuple<Args...> create(Fn &&fn) {
            return {fn(type_tag<Args>()) ...};
        }
    };

    class UnexpectedEndException : public std::exception {
    public:
        const char *what() const noexcept override {return "unexpected end of record while parsing a binary message";}
    };

    template<typename T, typename Iter>
    T from_binary(Iter &itr, Iter end) {
        if (itr != end) {
            if constexpr(std::is_same_v<T, bool>) {
                return from_binary<int>(itr, end) != 0;
            } else if constexpr(std::is_same_v<T, std::string_view>) {
                auto sz = from_binary<std::size_t>(itr, end);
                auto b = itr;
                std::advance(itr, sz);
                auto e = itr;
                return std::string_view(b, e);
            } else if constexpr(std::is_integral_v<T>) {
                if constexpr(std::is_unsigned_v<T>) {
                    T x = {};
                    int shift = 0;
                    bool c = true;
                    while (c && itr != end) {
                        T v = static_cast<T>(static_cast<unsigned char>(*itr));
                        ++itr;
                        x |= ((v & 0x7F) << shift);
                        shift += 7;
                        c = (v & 0x80) != 0;
                    }
                    return x;
                } else {
                    auto x = from_binary<std::make_unsigned_t<T> >(itr, end);
                    auto r = static_cast<T>(x >> 1);
                    if (x & 0x1) r = -(r+1);
                    return r;
                }
            } else if constexpr(is_container<T>) {
                auto sz = from_binary<std::size_t>(itr, end);
                T out;
                auto ins = std::inserter(out, out.end());
                while (sz && itr != end) {
                    *ins = from_binary<typename T::value_type>(itr, end);
                    ++ins;
                    --sz;
                }
                return out;
            } else if constexpr(std::is_trivially_copy_constructible_v<T>) {
                union CopyHelper {
                    char buffer[sizeof(T)];
                    T val;
                    CopyHelper() {}
                    ~CopyHelper() {}
                };
                CopyHelper tmp;
                char *c = tmp.buffer;
                char *ce = c + sizeof(T);
                while (c != ce && itr != end) {
                    *c = *itr;
                    ++c; ++itr;
                }
                return tmp.val;
            } else if constexpr(is_variant_type<T>) {
                unsigned int idx = from_binary<unsigned int>(itr, end);
                return to_constant<std::variant_size_v<T> >(idx,[&](auto c){
                    if constexpr (c.valid) {
                        return T(from_binary<std::variant_alternative_t<c.value, T> >(itr, end));
                    } else {
                        return T();
                    }
                });
            } else if constexpr(is_optional_type<T>) {
                T out;
                bool hv = from_binary<bool>(itr,end);
                if (hv) {
                    out = from_binary<T::value_type>(itr,end);
                }
                return out;
            } else if constexpr(is_tuple_type<T>) {
                return tuple_hlp<T>::create([&](auto tag){
                    using U = typename std::decay_t<decltype(tag)>::type;
                    return from_binary<U>(itr, end);
                });
            } else if constexpr(custom_serialize<T>){
                std::string s = from_binary<std::string>(itr, end);
                return custom_from_binary(std::in_place_type<T>,s);
            } else {
                static_assert(assert_error<T>, "This type cannot be stored in BinTuple");
                throw;
            }
        } else {
            throw UnexpectedEndException();
        }
    }

    template<typename T, typename Iter>
    T from_binary(Iter &&itr, Iter end) {
        return from_binary<T>(itr, end);
    }


    template<std::output_iterator<char> Iter >
    Iter to_binary_args(Iter iter) {return iter;}

    template<std::output_iterator<char> Iter,typename T, typename ... Args>
    Iter to_binary_args(Iter iter, const T &x, const Args &...args) {
        return to_binary_args<Iter, Args...>(to_binary(iter, x), args ...);
    }


}

template<typename T>
concept SerializableType = (std::is_same_v<T, bool> || std::is_same_v<T, std::string_view>
        || std::is_integral_v<T> || is_container<T> || std::is_trivially_copy_constructible_v<T>
        || is_variant_type<T> || is_optional_type<T> || is_tuple_type<T> || custom_serialize<T>);


///defines format, converts from tuple to string and from string to tuple
template<SerializableType ... Args>
class TupleBin{
public:

    using tuple_type = std::tuple<Args...>;

    static std::string compose(const Args & ... args) {
        std::string buff;
        Serializer::to_binary_args(std::back_inserter(buff), args...);
        return buff;
    }


    static std::string compose(const std::tuple<Args ...> &tup) {
        std::string buff;
        std::apply([&](const auto & ... args) {
            Serializer::to_binary_args(std::back_inserter(buff), args...);
        }, tup);
        return buff;
    }

    static std::tuple<Args...> parse(std::string_view s) {
        auto b = s.begin();
        auto e = s.end();
        return std::tuple<Args...>{Serializer::from_binary<Args>(b, e) ...};
    }

};


template<typename T>
concept is_TupleBin_type = is_tuple_type<typename T::tuple_type>;


}
