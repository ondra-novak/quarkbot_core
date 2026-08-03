#pragma once
#include "../json/json.hpp"
#include "../utils/lookup.hpp"
#include "serialize.hpp"
#include <type_traits>
#include <variant>


namespace srl {

constexpr auto layout_type_table =  make_string_lookup_table<LayoutType>({
    {LayoutType::sequence,"sequence"},
    {LayoutType::collection,"collection"},
    {LayoutType::dictionary,"dictionary"},
    {LayoutType::variant,"variant"},
    {LayoutType::optional,"optional"},
    {LayoutType::enumeration,"enumeration"},
    {LayoutType::string,"string"},
    {LayoutType::boolean,"boolean"},
    {LayoutType::fixed_uint,"fixed_uint"},
    {LayoutType::fixed_sint,"fixed_sint"},
    {LayoutType::floating,"floating"},
    {LayoutType::varuint,"varuint"},
    {LayoutType::varint,"varint"},
    {LayoutType::trivial,"trivial"},

});

inline Json serialize_schema_to_json(const Schema &schema) {
    Json::Object types;
    for (const auto &[key, value]: schema.schema) {
        Json::Array seq;
        bool any_name = false;
        Json::Array names;
        for (const auto &[type, name]: value->fields) {
            any_name = any_name || !name.empty();
            names.push_back(name.empty()?Json():Json(name));
            seq.push_back(type);
        }
        Json::Object obj;
        if (!seq.empty()) obj["fields"] = seq;
        if (any_name) obj["names"] = names;
        obj["layout"] = layout_type_table(value->type).value_or(std::string_view());
        //nonzero exactly for the fixed width leaf layouts - a schema driven reader
        //needs it to know how many bytes the field occupies
        if (value->byte_size) {
            obj["byte_size"] = value->byte_size;
        }
        types[key] = obj;
    }
    return Json{{"root", schema.root_type},
                 { "types", types}
        };
}


}