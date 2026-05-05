#pragma once
#include <type_traits>
namespace quarkbot {

template<typename T, typename Iter>
requires(std::is_unsigned_v<T>)
void big_endian_binarize(T x, Iter iter) {
    for (unsigned int i = 0; i < sizeof(T); ++i) {
        *iter++ = static_cast<char>((x >> ((sizeof(T)-1-i) * 8)) & 0xFF);
    }      
}

template<typename T, typename Iter>
requires(std::is_unsigned_v<T>)
Iter big_endian_unbinarize(T &x, Iter iter) {
    x = 0;
    for (unsigned int i = 0; i < sizeof(T); ++i) {
        x <<= 8;
        x |= static_cast<unsigned char>(*iter++);       
    }
    return iter;
};

}