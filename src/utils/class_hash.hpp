#pragma once

#include "utils/fnv1a.hpp"
#include <cstddef>
#include <source_location>

template<typename T>
struct ClassHash {
    static constexpr std::size_t hash() {
        return fnv1a_hash(std::source_location::current().function_name());
    }
};


template<typename T>
constexpr std::size_t class_hash = ClassHash<T>::hash();
