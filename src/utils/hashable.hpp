#pragma once

#include <functional>
template<typename T>
concept is_default_hashable = requires(T a) {
    { std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
};
    

template<typename T>
concept has_get_hash_function = requires(T a) {
    { a.get_hash() } -> std::convertible_to<std::size_t>;
};


template<typename T>
struct Hasher {
    std::size_t operator()(const T &obj) const {
        if constexpr (has_get_hash_function<T>) {
            return obj.get_hash();
        } else {
            static_assert((is_default_hashable<T>), "Type is not hashable");
            return std::hash<T>{}(obj);
        }
    }
};