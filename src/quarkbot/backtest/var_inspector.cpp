#include "var_inspector.hpp"
#include "quarkbot/common/deserialize_resolver.hpp"
#include "quarkbot/common/mem_storage.hpp"
#include "quarkbot/log.hpp"
#include "quarkbot/persistent.hpp"
#include "quarkbot/serializer/deserialize_from_schema.hpp"
#include <exception>

namespace quarkbot {

    void VariableInspector::attach_storage(Storage storage) {
        std::scoped_lock _(_mx);
        _storage = std::move(storage);
        _conn = _storage.add_replicator([this](const Storage::ReplicatorEvent &ev) noexcept {
            using Type = Storage::ReplicatorEvent::Type;
            std::scoped_lock _(_mx);
            if (ev.type == Type::put_schema) {
                try {
                    _schema_cache[ev.schema_hash] = Json::from_string(ev.value);
                } catch (const std::exception &e) {
                    logWarning("Failed to parse schema {}: {}", ev.schema_hash, e.what());
                }
            } else if (ev.type == Type::put_key_value) {
                //the record itself, not the last-revision pointer: a put with
                //UpdateLastRevision::disable emits no pointer event but still updates
                _updated_vars.insert(std::string(ev.name));
            }
        });
    }

    Json VariableInspector::deserialize_var(const auto &val) {
        if (!val.exists) {
            return Json(); //null
        }
        srl::SchemaHash h;
        val.extract(h,h);
        auto it = _schema_cache.find(h);
        if (it != _schema_cache.end()) {
            auto arch = srl::string_deserializer(val.data);
            try {
                return srl::deserialize_from_schema(it->second,arch, get_desrl_resolver());
            } catch (...) {
                return binary_content(val.data);
            }
            return srl::deserialize_from_schema(it->second,arch, get_desrl_resolver());
        } else {
            return binary_content(val.data);
        }

    }

    Json VariableInspector::inspect(const std::string_view &var_name) {
        std::scoped_lock _(_mx);
        return inspect_lk(var_name);
    }

    Json VariableInspector::inspect_lk(const std::string_view &var_name) {
        if (!_storage) {
            return Json(); //null
        }
        auto val = _storage.get(var_name);
        return deserialize_var(val);
    }
    Json::Object VariableInspector::inspect_all_updated() {
        std::scoped_lock _(_mx);
        Json::Object res;
        for (const auto &var: _updated_vars) {
            Json v = inspect_lk(var);
            if (!v.is_null()) {
                res[var] = v;
            }
        }
        return res;
    }
    Json::Array VariableInspector::inspect_series(const std::string_view &var_name){
        std::scoped_lock _(_mx);
        Json::Array res;
        if (!_storage) {
            return res;
        }
        for (const auto &val: _storage.select_range(var_name, RecordKey::min(), RecordKey::max())) {
            res.push_back(deserialize_var(val));
        }
        return res;
    }
    void VariableInspector::flush() {        
        try {
            shared_transaction({}, CommitMode::lazy); //will throw exception, but will flush pending transaction
        } catch (...) {
            //ignore exception, we just want to flush pending transaction
        }
    }
    void VariableInspector::clear_updated() {
        std::scoped_lock _(_mx);
        _updated_vars.clear();
    }

}