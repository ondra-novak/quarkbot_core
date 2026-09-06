///@file companion header defines all methods that using serializer

#pragma once

#include "abstract/istorage.hpp"
#include "serializer/schema_fwd.hpp"
#include "serializer/serialize.hpp"
#include "storage.hpp"
#include "serializer/serialize_schema_to_json.hpp"
#include <iterator>
namespace quarkbot {

inline std::pair<std::optional<srl::SchemaHash>, std::string_view> extrach_schema_hash(std::string_view value) {
    if (value.size() < sizeof(srl::SchemaHash)) return {std::nullopt, value};
    std::array<char, sizeof(srl::SchemaHash)> buff;
    std::string_view tail = value.substr(value.size() - sizeof(srl::SchemaHash));
    std::copy(tail.begin(), tail.end(),buff.begin());
    value.remove_suffix(sizeof(srl::SchemaHash));
    return {std::bit_cast<srl::SchemaHash>(buff), value}; 
}

template<typename T>
inline bool extract_srl(std::string_view value, T &out, srl::SchemaHash &type_hash) {
    auto ex = extrach_schema_hash(value);
    if (!ex.first) return false;
    value = ex.second;
    type_hash = ex.first.value();
        
    if (type_hash != srl::schema_hash<T>) return false;

    bool valid = false;
    try {
        srl::deserialize_from(value.begin(), value.end(), out);
        valid = true;
    } catch (...) {
        valid = false;
    }
    return valid;

}

template<typename Self, typename T>
inline bool IStorage::Extractor::extract(this Self &&self, T &out, srl::SchemaHash &h)             {
    if (!self.exists || self.data.size() < sizeof(srl::SchemaHash)) return false;
    return extract_srl<T>(self.data,out, h);    
}


template<typename T>
std::string StorageTransaction::serialize_value(const T &val) {    

    std::vector<char> buffer;
    auto iter = srl::serialize_to<char>(val, std::back_inserter(buffer));
    auto hash = srl::schema_hash<T>;
    auto schema_bin = std::bit_cast<std::array<char, sizeof(srl::SchemaHash)> >(hash);
    std::copy(schema_bin.begin(), schema_bin.end(), iter);

    if (!get_storage().get_handle()->is_schema_stored(hash)) {
        auto schema = srl::Schema::create<T>();
        auto js = srl::serialize_schema_to_json(schema);
        put_schema_binary(hash, js.to_string());
    }
    return {buffer.begin(), buffer.end()};
}

}