#include "persistent_reporter.hpp"
#include "quarkbot/common/deserialize_resolver.hpp"
#include "quarkbot/common/mem_storage.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/json/json.hpp"
#include "quarkbot/log.hpp"
#include "quarkbot/serializer/deserialize_from_schema.hpp"
#include "quarkbot/serializer/schema_fwd.hpp"
#include "quarkbot/serializer/serialize.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/storage_srl.hpp"
#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <print>
#include <ostream>
#include <unordered_map>

namespace quarkbot {

template<typename T> 
void report_variable_type(std::ostream &out, std::string_view key, std::string_view value, T &&buffer) {
    auto tp = std::chrono::time_point_cast<std::chrono::milliseconds>(ExecutionWorker::current().now());
    srl::SchemaHash h;
    extract_srl(value,buffer, h);
    std::print(out,"{:%Y-%m-%d %H:%M:%S} {}={}\n", tp, key, buffer);
}




static auto reporter_replicator(std::ostream &out) {
    std::unordered_map<srl::SchemaHash, Json> schema_cache;
    return [&out, schema_cache](const Storage::ReplicatorEvent &ev)mutable noexcept{
        try {
            if (ev.schema_hash) {
                schema_cache.emplace(schema_key_to_hash(ev.key).value(), Json::from_string(ev.value));            
            } else {
                srl::SchemaHash hash;
                std::nullptr_t dummy;
                extract_srl(ev.value, dummy, hash);
                auto schiter = schema_cache.find(hash);
                Json jval;
                if (schiter == schema_cache.end()) {
                    jval = binary_content(ev.value);
                } else {
                    auto arch = srl::string_deserializer(ev.value);
                    jval = srl::deserialize_from_schema(schiter->second, arch,get_desrl_resolver());
                }
                auto now = ExecutionWorker::current().now();
                std::println(out, "{:%Y-%m-%d %H:%M:%S}\t{}\t{}", now,ev.key, jval.to_string());
            }
        } catch (const std::exception &e) {
            logError("Execption in variable renderer: {}, key={}", e.what(), ev.key);
        }
    };
}


Storage::Replicator::Connection connect_variable_reporter(Storage stor, std::ostream &out) {    
    return stor.add_replicator(reporter_replicator(out));
}

Storage::Replicator::Connection connect_variable_reporter(Storage stor, std::filesystem::path output) {
    auto file = std::make_shared<std::ofstream>(output);
    if (!(*file)) throw std::runtime_error(std::format("Failed to open variable reporter log for writting: {}", output.string()));
    auto repl = reporter_replicator(*file);    
    return stor.add_replicator([repl, file](const Storage::ReplicatorEvent &x) mutable noexcept{
        repl(x);
    });

}

}