#include "../json/json.hpp"
#include "serialize.hpp"
#include <type_traits>
#include <variant>


namespace srl {

inline Json serialize_schema_to_json(const BinarySchemaGenerator::Schema &schema) {
    Json::Object types;
    for (const auto &[key, value]: schema.map) {
        Json::Array seq;
        for (const auto &[type, name]: value.fields) {
            if (name.has_value()) {
                seq.push_back(Json{name.value(), type});
            } else {
                seq.push_back(Json(type));
            }
        }
        Json::Object obj;
        if (!seq.empty()) obj["fields"] = seq;
        if (value.layout.has_value()) obj["layout"] = value.layout.value().name;
        if (value.size.has_value()) obj["size"] = value.size.value();
        types[key] = obj;
    }
    return Json{{"root", schema.root},
                 { "types", types}
        };
}


}