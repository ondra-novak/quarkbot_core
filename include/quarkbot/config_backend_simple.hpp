#pragma once

#include "quarkbot/config.hpp"
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
namespace quarkbot {

class IConfigBackend {
public:
    virtual std::optional<std::string_view> operator()(const std::string &) const = 0;
    virtual ~IConfigBackend() = default;
};


class ConfigBackend {
public:
    constexpr ConfigBackend() = default;
    ConfigBackend(std::shared_ptr<const IConfigBackend> backend):_backend(backend) {}
    std::optional<std::string_view> operator()(const std::string &key) const {
        return _backend?_backend->operator()(key):std::nullopt;
    }
protected:
    std::shared_ptr<const IConfigBackend> _backend = {};
};

class ConfigBackendMap final : public IConfigBackend , public std::unordered_map<std::string, std::string>{
public:
    using std::unordered_map<std::string, std::string>::unordered_map;

    virtual std::optional<std::string_view> operator()(const std::string &key) const {
        auto iter = this->find(key);
        if (iter == this->end()) return std::nullopt;
        else return iter->second;
    }
};

static_assert(ConfigSource<ConfigBackend>);

}