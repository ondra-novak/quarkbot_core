#pragma once

#include "config.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
namespace quarkbot {
class IConfigBackendWithPaths {
public:
    using Out =  std::optional<std::pair<std::string_view, const std::filesystem::path *> >;
    virtual Out operator()(const std::string &) const = 0;
    virtual ~IConfigBackendWithPaths() = default;
};


class ConfigBackendWithPaths {
public:
    constexpr ConfigBackendWithPaths() = default;
    ConfigBackendWithPaths(std::shared_ptr<const IConfigBackendWithPaths> backend):_backend(backend) {}
    IConfigBackendWithPaths::Out operator()(const std::string &key) const {
        return _backend?_backend->operator()(key):std::nullopt;
    }
protected:
    std::shared_ptr<const IConfigBackendWithPaths> _backend = {};
};

class ConfigBackendWithPathsMap final : public IConfigBackendWithPaths {
public:
    using PathSet = std::unordered_set<std::filesystem::path>;
    using KeyValue = std::unordered_map<std::string, std::pair<std::string, const std::filesystem::path *> >;

    void set(std::string key, std::string value, std::filesystem::path p) {
        auto ins = _paths.insert(std::move(p));
        const auto *ptr = &(*ins.first);
        _kv[std::move(key)] = std::pair(std::move(value), ptr);
    }
    void set(std::string key, std::string value) {
        _kv[std::move(key)] = std::pair(std::move(value), nullptr);
    }

    const KeyValue &get_map() const {return _kv;}
    KeyValue &get_map() {return _kv;}

    void clear() {
        _kv.clear();
        _paths.clear();
    }

    bool empty() const {
        return _kv.empty();
    }


    virtual IConfigBackendWithPaths::Out operator()(const std::string &key) const {
        auto iter = _kv.find(key);
        if (iter == _kv.end()) return std::nullopt;
        else return IConfigBackendWithPaths::Out::value_type{iter->second.first, iter->second.second};
    }
protected:
    PathSet _paths;
    KeyValue _kv;

};

static_assert(ConfigSource<ConfigBackendWithPaths>);


}
