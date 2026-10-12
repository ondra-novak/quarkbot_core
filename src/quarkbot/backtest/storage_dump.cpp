#include "storage_dump.hpp"
#include "quarkbot/common/deserialize_resolver.hpp"
#include "quarkbot/json/json.hpp"
#include "quarkbot/serializer/deserialize_from_schema.hpp"
#include "quarkbot/storage_srl.hpp"
#include <algorithm>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>

namespace quarkbot {

static std::string csv_field(std::string_view s) {
    if (s.find_first_of(",\"\r\n") == s.npos) return std::string(s);
    std::string out("\"");
    for (char c: s) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

void dump_storage_csv(const Storage &storage, std::ostream &out) {
    std::unordered_map<srl::SchemaHash, Json> schema_cache;

    auto decode = [&](std::string_view data) -> Json {
        auto [hash, body] = extrach_schema_hash(data);
        if (!hash) return binary_content(data);
        auto iter = schema_cache.find(*hash);
        if (iter == schema_cache.end()) {
            auto sch = storage.get_schema(*hash);
            if (!sch.exists) return binary_content(data);
            iter = schema_cache.emplace(*hash, Json::from_string(sch.data)).first;
        }
        try {
            auto arch = srl::string_deserializer(data);
            return srl::deserialize_from_schema(iter->second, arch, get_desrl_resolver());
        } catch (...) {
            return binary_content(data);
        }
    };

    std::println(out, "key,ordered,random,value");
    auto vars = storage.list();
    std::sort(vars.begin(), vars.end());
    for (const auto &var: vars) {
        auto name = csv_field(var);
        for (const auto &val: storage.select_range(var, RecordKey::min(), RecordKey::max())) {
            std::println(out, "{},{},{},{}", name, val.key.ordered, val.key.random,
                         csv_field(decode(val.data).to_string()));
        }
    }
}

}
