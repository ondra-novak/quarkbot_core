#pragma once

#include <string>
#include <random>
#include <stdexcept>

template<typename Generator>
inline std::string generate_random_string(Generator &gen, size_t length = 15) {
    if (length == 0) {
        throw std::invalid_argument("Length must be greater than 0");
    }
    
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const size_t charset_size = sizeof(charset) - 1;
    
    std::uniform_int_distribution<> dis(0, charset_size - 1);
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    
    return result;
}

