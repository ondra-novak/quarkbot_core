#pragma once

#include "fnv1a.hpp"
#include <concepts>
#include <cstddef>
#include <source_location>


template<typename T>
concept HasEnforcedClassHash = requires {
    {T::enforced_class_hash} -> std::same_as<std::size_t>;    
};

template<typename T>
concept HasClassName = requires {
    {T::class_name} -> std::same_as<std::string_view>;    
};

template<typename T>
struct ClassHash {
    static constexpr std::size_t hash() {
        return fnv1a_hash(std::source_location::current().function_name());
    }
};

template<typename T>
requires(HasEnforcedClassHash<T>)
struct ClassHash<T> {
    static constexpr std::size_t hash() { return T::enforced_class_hash;}
};

template<typename T>
requires(HasClassName<T>)
struct ClassHash<T> {
    static constexpr std::size_t hash() {
        return fnv1a_hash(T::class_name);
    }
};


template<typename T>
constexpr std::size_t class_hash = ClassHash<T>::hash();
