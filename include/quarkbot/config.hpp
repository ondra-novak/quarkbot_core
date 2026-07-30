#pragma once

#include "quarkbot/utils/refcnt.hpp"
#include "types.hpp"
#include <cstddef>
#include <optional>
#include <unordered_map>
namespace quarkbot {

///Generic configuration class for strategy parameters
/**
Config class provides access to strategy parameters through a simple key-value interface. 
It supports nested sections using operator/ and automatic parsing of values to various types, 
including bool, arithmetic types, enums with string lookup and types with from_string method.
The actual source of configuration values is abstracted through a user-provided callable, 
which takes a string key and returns an optional string value. This allows for flexible configuration sources, 
such as files, environment variables, command line arguments, or even dynamic configuration services.

@tparam Source type of the configuration source, must be callable with signature std::optional<std::string_view>(std::string_view)
Expected underlying source is key-value store, where keys are strings and values are strings. The source should return std::nullopt for missing keys.
*/
template<typename Source>
requires (std::is_invocable_r_v<std::optional<std::string_view>, Source, const std::string &>)
class ConfigT {
public:

    ///default constructor, creates empty configuration
    constexpr ConfigT() = default;
    ///constructor from source and separator character for sections
    constexpr ConfigT(Source source, char separator = '/'):_source(std::move(source)), _sepatator(separator) {}


    struct OptionalValue;

    ///Value wrapper for configuration values, provides automatic parsing to various types
    struct Value {
        std::string key;
        std::optional<std::string_view> value;


        ///automatic conversion operator to various types, enabled for bool, arithmetic types, enums with string lookup and types with from_string method
        template<typename T> 
        requires(std::is_same_v<T, bool> 
                || std::is_arithmetic_v<T> 
                || HasFromStringMethod<T> 
                || HasStringLookup<T> 
                || std::is_constructible_v<T, std::string_view>)
        constexpr operator T() const {
            if (!value.has_value()) {
                 throw std::runtime_error(std::format("Key not found in configuration: {}", key));
            }

            std::string_view actual_str = *value;
            

            if constexpr (std::is_same_v<T, bool>) {
                if (actual_str == "true" || actual_str == "1" || actual_str == "on" || actual_str=="yes" || actual_str == "enabled")  return true;
                if (actual_str == "false" || actual_str == "0" || actual_str == "off" || actual_str=="no" || actual_str == "disabled")  return false;
                throw std::runtime_error(std::format("Config parse error on {}: Boolean expects 'true' or 'false', 1 or 0, 'on' or 'off', 'enabled' or 'disabled'", key));
            } else if constexpr(std::is_arithmetic_v<T>) {
                T val;
                auto r = std::from_chars(actual_str.begin(), actual_str.end(), val);
                if (r.ec != std::errc{} || r.ptr != actual_str.end())  {
                    throw std::runtime_error(std::format("Config parse error on {}: invalid number format", key));
                }
                return val;
            } else if constexpr(std::is_constructible_v<T, std::string_view>) {
                return T(actual_str);            
            } else if constexpr(HasFromStringMethod<T>) {
                try {
                    return T::from_string(actual_str);
                } catch (const std::exception &e) {
                    throw std::runtime_error(std::format("Config parse error on `{}` into `{}`: {} ", key, typeid(T).name(), e.what()));
                } catch (...) {
                    throw std::runtime_error(std::format("Config parse error on `{}`: failed to parse into {}", key, typeid(T).name()));
                }
            } else if constexpr(HasStringLookup<T>){
                auto r = string_lookup<T>(actual_str);
                if (r.has_value()) return r.value();
                std::string lst;
                for (auto &[k,v]:string_lookup<T>) {
                    lst.append(v);
                    lst.push_back(',');
                }
                if (!lst.empty()) lst.pop_back(); else lst = "<empty>";
                throw std::runtime_error(std::format("Config parse error on `{}`: enumeration value is not in table: `{}`. Defined values: {}",
                    key, actual_str, lst
                ));
            } else {
                static_assert(assert_false<T>, "Cannot read such type");
            }
        }    

        ///overload of operator() to provide default value if key is not found,
        template<typename T> 
        constexpr auto operator()(const T &val) const -> decltype(std::declval<Value>().operator T()) {
            if (value.has_value()) return *this;
            else return val;
        }    

        ///specifies that default value is nullopt which expects result as std::optional<T> 
        constexpr OptionalValue operator()(std::nullopt_t) const;
        
        template<std::size_t n>
        constexpr std::string_view operator()(const char (&def)[n]) const {
            return this->operator()(std::string_view(def, n));
        }
    };

    struct OptionalValue {
        const Value &val;

        template<typename X>
        constexpr operator std::optional<X>() const {
            if (val.value.has_value()) return val.operator X();
            else return std::nullopt;
        }
    };


    ///access configuration value by key, returns Value wrapper which can be automatically converted to desired type
    constexpr Value operator[](std::string_view key) const {
        Value out {{}, {}};
        build_whole_key(out.key, key);
        out.value = _source(out.key);
        return out;
    }

    ///create sub-configuration for a section, section name is added as prefix to keys in the sub-configuration
    constexpr ConfigT operator/(std::string_view section) const {
        return ConfigT(*this, _prefix, section);
    }

        ///create sub-configuration for a section, section name is added as prefix to keys in the sub-configuration
    constexpr ConfigT section(std::string_view section) const {
        return ConfigT(*this, _prefix, section);
    }

    constexpr ConfigT parent() const {
        return ConfigT(*this, nullptr);
    }


protected:

    ///constructor for creating sub-configurations with prefix
    constexpr ConfigT(const ConfigT &parent, std::string_view cur_prefix, std::string_view new_prefix)
        :_source(parent._source), _sepatator(parent._sepatator),_prefix(std::format("{}{}{}", cur_prefix, new_prefix, parent._sepatator)) {}
    constexpr ConfigT(const ConfigT &parent, std::nullptr_t)
        :_source(parent._source), _sepatator(parent._sepatator),_prefix(get_parent(parent._prefix)) {}


    Source _source;
    char _sepatator = '/';
    std::string _prefix = {};

    constexpr void build_whole_key(std::string &target, std::string_view key) const {
        target.reserve(key.size()+_prefix.size());   
        target.append(_prefix);
        target.append(key);
    }
    constexpr std::string get_parent(std::string prefix){
        if (prefix.empty()) return prefix;
        char sep = prefix.back();
        prefix.pop_back();
        while (!prefix.empty() && prefix.back() != sep) prefix.pop_back();
        return prefix;

    }

};

template<typename Source>
requires (std::is_invocable_r_v<std::optional<std::string_view>, Source, const std::string &>)
inline constexpr typename ConfigT<Source>::OptionalValue ConfigT<Source>::Value::operator()(std::nullopt_t) const {
            return {*this};
}


struct ConfigStringReader {
    std::string_view data;
    std::string_view operator()() {return std::exchange(data,"");}
};


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

}