#pragma once

#include "quarkbot/json/json.hpp"
#include "quarkbot/serializer/schema_fwd.hpp"
#include "quarkbot/storage.hpp"
#include <unordered_map>
#include <unordered_set>
namespace quarkbot {

class VariableInspector {
public:

    ///inspects the current value of a variable in the storage
    Json inspect(const std::string_view &var_name);
    ///inspects the current value of a variable in the storage as a series of values
    Json::Array inspect_series(const std::string_view &var_name);
    Json::Object inspect_all_updated();


    ///flushes all pending updates to the variable reporter
    static void flush();

    void clear_updated() {_updated_vars.clear();}
    
    ///attaches a storage instance to the variable inspector
    void attach_storage(Storage storage);


protected:
    std::mutex _mx;
    Storage _storage;
    Storage::Replicator::Connection _conn;
    std::unordered_map<srl::SchemaHash, Json> _schema_cache;
    std::unordered_set<std::string> _updated_vars;

    Json deserialize_var(const auto &val);
    Json inspect_lk(const std::string_view &var_name);
};

}