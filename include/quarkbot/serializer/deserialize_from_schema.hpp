#pragma once

#include "../json/json.hpp"
#include "serialize.hpp"
#include <concepts>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace srl {


template<typename T>
concept ResolveCustomType = requires(T cb, std::string_view type_name, std::string_view content) {
    {std::invoke(cb, type_name, content)} -> std::convertible_to<Json>;
};

inline std::string default_resolver(std::string_view , std::string_view content){
    return std::format("Binary size: {}", content.size());
}

inline Json deserialize_from_schema(const Json &json_schema, std::string_view type, auto &arch, ResolveCustomType auto &resolver) {
    const auto &typedf = json_schema["types"][type];
    if (!typedf.is_object()) throw std::runtime_error(std::format("Corrupted json schema: Missing type: {}", type));
    auto ltop = layout_type_table(typedf["layout"].as_text());
    if (!ltop) throw std::runtime_error(std::format("Corrupted json schema: Unknown layout for type: {}", type));
    LayoutType ltype = *ltop;

    auto trivial_parse = [](const Json &typedf, std::string_view type, auto &arch, ResolveCustomType auto &resolver){
            auto bsize = typedf["byte_size"].as<std::size_t>();
            std::string buffer;
            buffer.resize(bsize);
            read_binary(arch, std::span(reinterpret_cast<std::uint8_t *>(buffer.data()), buffer.size()));
            return resolver(type, buffer);
    };

    auto collection_parse = [](const Json &typedf, const Json &json_schema, auto &arch, ResolveCustomType auto &resolver) {
            const Json &fld = typedf["fields"][0];
            std::size_t sz;
            arch(sz);
            Json::Array out;
            for (std::size_t i = 0; i < sz; ++i){
                out.push_back(deserialize_from_schema(json_schema, fld.as_text(), arch, resolver));
            }
            return out;
    };

    switch (ltype) {
        case LayoutType::boolean: {
            bool val;
            arch(val);
            return val;
        }
        case LayoutType::sequence:{
            std::size_t idx = 0;
            auto names = typedf["names"];
            if (names.is_array()){
                Json::Object out;
                for (const auto &f: typedf["fields"].as_array()) {
                    const Json &n = typedf["names"][idx];
                    ++idx;
                    auto name = n.as<std::string>();
                    if (name.empty()) {
                        name = std::format("#{}", idx);
                    }
                    out.push_back({name, deserialize_from_schema(json_schema, f.as_text(), arch, resolver)});                    
                }
                return out;
            } else {
                Json::Array out;
                for (const auto &f: typedf["fields"].as_array()) {
                    out.push_back(deserialize_from_schema(json_schema, f.as_text(), arch, resolver));                    
                }
                return out;
            }
        }
        case LayoutType::collection: {
            return collection_parse(typedf, json_schema, arch,resolver);
        }
        case srl::LayoutType::dictionary:{
            const Json &key = typedf["fields"][0];
            const Json &value = typedf["fields"][1];
            std::size_t sz;
            arch(sz);
            Json::Array out;
            for (std::size_t i = 0; i < sz; ++i){
                auto k = deserialize_from_schema(json_schema, key.as_text(), arch, resolver);
                auto v = deserialize_from_schema(json_schema, value.as_text(), arch, resolver);
                out.push_back(Json{k,v});
            }
            return out;
        }
        case srl::LayoutType::enumeration: {
            int val;
            arch(val);
            return val;
        }
        case srl::LayoutType::fixed_sint: {
            auto bsize = typedf["byte_size"].as<std::size_t>();
            switch (bsize) {
                case 1: {char v;arch(v);return v;}
                case 2: {std::int16_t v;arch(v);return v;}
                default: return trivial_parse(typedf, type, arch, resolver);
            }
        }
        case srl::LayoutType::fixed_uint: {
            auto bsize = typedf["byte_size"].as<std::size_t>();
            switch (bsize) {
                case 1: {unsigned char v;arch(v);return v;}
                case 2: {std::uint16_t v;arch(v);return v;}
                default: return trivial_parse(typedf, type, arch, resolver);
            }
        }
        case srl::LayoutType::floating: {
            auto bsize = typedf["byte_size"].as<std::size_t>();
            switch (bsize) {
                case 4: {float v;arch(v);return v;}
                case 8: {double v;arch(v);return v;}
                case 16: {long double v;arch(v);return v;}
                default: return trivial_parse(typedf, type, arch, resolver);
            }
        }
        case srl::LayoutType::optional: {
            bool b;
            arch(b);
            if (b) return deserialize_from_schema(json_schema, typedf["fields"][0].as_text(), arch, resolver);
            else return nullptr;
        }
        case srl::LayoutType::string:{
            auto fld = typedf["fields"][0].as_text();
            if (fld == "char") {
                std::string out;
                arch(out);
                return out;
            } else {
                return collection_parse(typedf, json_schema, arch,resolver);
            }
        }
        case srl::LayoutType::trivial: 
            return trivial_parse(typedf, type, arch, resolver);
        case srl::LayoutType::variant: {
            std::size_t vr;
            arch(vr);
            auto fld = typedf["fields"][vr];
            return deserialize_from_schema(json_schema, fld.as_text(), arch, resolver);            
        }
        case srl::LayoutType::varint: {
            std::intmax_t val;
            arch(val);
            return val;
        }
        case srl::LayoutType::varuint: {
            std::uintmax_t val;
            arch(val);
            return val;
        }            
    }
    std::unreachable();

}

inline Json deserialize_from_schema(const Json &json_schema, auto &arch, ResolveCustomType auto &&resolver) {
    return deserialize_from_schema(json_schema, json_schema["root"].as_text(), arch, resolver);
}

}