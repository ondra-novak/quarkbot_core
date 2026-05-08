#pragma once

#include <cstdint>
#include <string_view>


namespace network {

    constexpr bool fast_is_space(char c) {
        return c == ' ' || c == '\t' || c =='\n' || c == '\r' || c == '\f';
    }

    constexpr std::string_view trim(std::string_view text) {
        while (!text.empty() && fast_is_space(text.front()))  text = text.substr(1);
        while (!text.empty() && fast_is_space(text.back())) text = text.substr(0,text.size()-1);
        return text;
    }

    constexpr std::string_view split(std::string_view &text, std::string_view sep) {
        std::string_view ret;
        auto n = text.find(sep);
        if (n == text.npos) {
            ret = text;
            text = {};
        } else {
            ret = text.substr(0,n);
            text = text.substr(n+sep.size());
        }
        return ret;
    }

    constexpr char fast_to_upper(char c) {
        if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
        return c;
    }

    constexpr int compare_icase(std::string_view a, std::string_view b) {
        auto cmn = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < cmn; ++i) {
            int ac = static_cast<std::uint8_t>(fast_to_upper(a[i]));
            int bc = static_cast<std::uint8_t>(fast_to_upper(b[i]));
            int df = ac - bc;
            if (df) return df;        
        }
        if (a.size() > b.size()) return 1;
        if (a.size() < b.size()) return -1;
        return 0;
    }


}