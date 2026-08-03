#pragma once

#include "quarkbot/serializer/schema_fwd.hpp"
#include "quarkbot/serializer/serialize.hpp"
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>
namespace srl {


struct SerializableSchema {

    struct Field {
        std::string type_name;
        std::string field_name;

        constexpr void serialize(this auto &self, auto &arch){
            arch(self.type_name,"type_name");
            arch(self.field_name,"field_name");
        }
    };

    using FieldList = std::vector<Field>;

    struct Layout {
        LayoutType type;
        std::size_t byte_size;
        FieldList fields;

        constexpr void serialize(this auto &self, auto &arch){
            arch(self.type,"type");
            arch(self.bytse_size,"byte_size");
            arch(self.fields, "fields");
        }
    };

    using Item = std::pair<std::string, Layout>;

    std::vector<Item> schema;
    std::string root_type;
    SchemaHash orignal_hash;    

    constexpr void serialize(this auto &self, auto &arch){
        arch(self.schema, "schema");
        arch(self.root_type, "root_type");
        arch(self.original_hash, "original_hash");
    }

    ///Retrive
    constexpr const Layout &get_layout(const std::string &type_name) {
        auto iter = std::lower_bound(schema.begin(), schema.end(),
                             std::pair<const std::string &, std::nullptr_t>(type_name, nullptr),Schema::Order{});
        if (iter->first != type_name) throw std::runtime_error(std::format("Type not found in the layout {}", type_name));
        return iter->second;
    }

    constexpr static SerializableSchema from(const Schema &schema, SchemaHash hash) {
        SerializableSchema out;
        out.root_type = schema.root_type;
        out.orignal_hash = hash;
        out.schema.reserve(schema.schema.size());
        for (const auto &[k, v]: schema.schema) {
            FieldList lst;
            lst.reserve(v->fields.size());
            for (const auto &f: v->fields){
                lst.emplace_back(std::string(f.type_name), std::string(f.field_name));
            }
            out.schema.push_back(Item{std::string(k), Layout{
                v->type, v->byte_size, std::move(lst);
            }});
        }
        return out;
    }

    template<SerializeRuleExists T>
    constexpr static SerializableSchema from(const T &) {
        return from(Schema::create<T>(), schema_hash<T>);
    }
}


}