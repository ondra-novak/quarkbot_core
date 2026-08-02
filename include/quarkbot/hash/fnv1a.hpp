#pragma once


#include <cstdint>
#include <string_view>

constexpr std::size_t fnv1a_hash(std::string_view str) {
 
    std::size_t hash = 0xcbf29ce484222325ULL;
    
    constexpr std::size_t prime = 0x100000001b3ULL;

    for (char c : str) {
        hash ^= static_cast<uint64_t>(c);
        hash *= prime;
    }

    return hash;
}

constexpr std::size_t hash_combine(std::size_t h1, std::size_t h2)
{
    
    h1 ^= h2 + 0x9e3779b9 + (h1<<6) + (h1>>2);
    return h1;
}