///@file companion header defines all methods that using serializer

#pragma once

#include "quarkbot/abstract/istorage.hpp"
#include "quarkbot/serializer/schema.hpp"
#include "quarkbot/storage.hpp"
namespace quarkbot {

template<typename Self, typename T>
inline bool IStorage::Extractor::extract(this Self &&self, T &val, srl::SchemaHash &h)             {
    if (!self.exists || self.data.size() < sizeof(srl::SchemaHash)) return false;

    std::array<char, sizeof(srl::SchemaHash)> buff;
    std::string_view value(self.data);
    std::string_view tail = value.substr(value.size() - sizeof(srl::SchemaHash));
    std::copy(tail.begin(), tail.end(),buff.begin());
    
    srl::SchemaHash record_hash = std::bit_cast<srl::SchemaHash>(buff);
    
    auto th = srl::schema_hash_for_type<T>;
    if (!th.has_value()) {
        std::string schema_bin = srl::serialize_schema<T>();
        std::hash<std::string> hasher;
        th = hasher(schema_bin);
    }

    h = th.value();

    if (record_hash != h) return false;

    bool valid = false;
    try {
        srl::BinaryParser parser([&, pos = std::size_t(0)](std::span<std::uint8_t> buff) mutable {
            if (pos + value.size() > value.size()) throw true;
            std::copy_n(value.begin()+static_cast<std::ptrdiff_t>(pos), buff.size(), buff.begin());
            pos += buff.size();
        });
        parser(val);
        valid = true;
    } catch (...) {
        valid = false;
    }
    return valid;
}

inline auto Storage::get_schema(srl::SchemaHash h) const  {
    std::optional<srl::BinarySchemaGenerator::Schema> out;
    out.emplace();
    if (!(get_schema_binary(h) >> out.value())) {
        out.reset();
    }
    return out;
    
}

template<typename T>
std::string StorageTransaction::serialize_value(const T &val) {
    std::vector<char> buffer;
    buffer.reserve(sizeof(val));
    srl::BinarySerializer serializer([&](std::span<const std::uint8_t> data){
        buffer.insert(buffer.end(), data.begin(), data.end());
    });
    serializer(val);
    auto hash = srl::schema_hash_for_type<T>;
    if (!hash.has_value() || !srl::schema_is_stored<T>) {
        std::string binschema = srl::serialize_schema<T>();
        std::hash<std::string> hasher;
        hash = srl::schema_hash_for_type<T> = hasher(binschema);
        srl::schema_is_stored<T> = true;
        put_schema_binary(hash.value(), binschema);
    }
    auto hash_bin = std::bit_cast<std::array<char, sizeof(srl::SchemaHash)> >(hash.value());
    buffer.insert(buffer.end(), hash_bin.begin(), hash_bin.end());            
    return {buffer.begin(), buffer.end()};
}

}