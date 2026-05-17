#pragma once


#include <cstdint>
#include <string_view>

constexpr uint64_t fnv1a_hash(std::string_view str) {
 
    uint64_t hash = 0xcbf29ce484222325ULL;
    
     const uint64_t prime = 0x100000001b3ULL;

    for (char c : str) {
        hash ^= static_cast<uint64_t>(c);
        hash *= prime;
    }

    return hash;
}