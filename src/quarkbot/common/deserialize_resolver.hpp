#pragma once

#include "quarkbot/decimal.hpp"
#include "quarkbot/json/json.hpp"
#include "quarkbot/serializer/serialize.hpp"
#include <string_view>
namespace quarkbot {

inline constexpr Json binary_content(std::string_view text) {
    if (text.empty()) return Json();
    std::string hex;
    hex.reserve(text.size()*2);
    for (char c: text) {
        unsigned char uc = static_cast<unsigned char>(c);
        auto lv = uc & 0xF;
        auto hv = uc >> 4;
        hex.push_back(static_cast<char>(hv +(hv<10?'0':'A'-10)));
        hex.push_back(static_cast<char>(lv +(lv<10?'0':'A'-10)));
    }
    std::string txt;
    for (char c: text) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 32 && uc < 128) {
            txt.push_back(c);
        } else{
            txt.push_back('.');
        }
    }
    return Json({std::move(hex), std::move(txt)});
}


inline constexpr auto get_desrl_resolver() {
    return [](std::string_view type, std::string_view content) -> Json {
        auto desrl = srl::string_deserializer(content);
        if (type == "Decimal" && content.size() == sizeof(Decimal)) {
            Decimal out;
            desrl(out);
            return JsonNumber(out.to_string());
        } else {
            return binary_content(content);
        }
    };
}
    
}