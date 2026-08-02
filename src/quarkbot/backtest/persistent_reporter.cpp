#include "persistent_reporter.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/serializer/schema_fwd.hpp"
#include "quarkbot/serializer/serialize.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/storage_srl.hpp"
#include <chrono>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace quarkbot {

template<typename T> 
void report_variable_type(std::ostream &out, std::string_view key, std::string_view value, T &&buffer) {
    auto tp = std::chrono::time_point_cast<std::chrono::milliseconds>(ExecutionWorker::current().now());
    srl::SchemaHash h;
    extract_srl(value,buffer, h);
    std::print(out,"{:%Y-%m-%d %H:%M:%S} {}={}\n", tp, key, buffer);
}





Storage::Replicator::Connection connect_variable_reporter(Storage stor, std::filesystem::path output) {
    auto file = std::make_shared<std::ofstream>(output);
    if (!(*file)) throw std::runtime_error(std::format("Failed to open variable reporter log for writting: {}", output.string()));
    return stor.add_replicator([file](const Storage::ReplicatorEvent &ev) noexcept{
        if (ev.schema_hash) return;
        srl::SchemaHash hash;
        std::nullptr_t dummy;
        extract_srl(ev.value, dummy, hash);
        switch (hash) {
            case srl::schema_hash<double>: report_variable_type(*file, ev.key, ev.value, double{}); break;
            case srl::schema_hash<char>: report_variable_type(*file, ev.key, ev.value, char{}); break;
            case srl::schema_hash<Decimal>: report_variable_type(*file, ev.key, ev.value, Decimal{}); break;
            case srl::schema_hash<int>: report_variable_type(*file, ev.key, ev.value, int{}); break;
            case srl::schema_hash<unsigned int>: report_variable_type(*file, ev.key, ev.value, static_cast<unsigned int>(0)); break;
            case srl::schema_hash<long>: report_variable_type(*file, ev.key, ev.value, long{}); break;
            case srl::schema_hash<unsigned long>: report_variable_type(*file, ev.key, ev.value, static_cast<unsigned long>(0)); break;
            case srl::schema_hash<long long>: report_variable_type(*file, ev.key, ev.value, static_cast<long long>(0)); break;
            case srl::schema_hash<unsigned long long>: report_variable_type(*file, ev.key, ev.value, static_cast<unsigned long long>(0)); break;
        }
    });
}

}