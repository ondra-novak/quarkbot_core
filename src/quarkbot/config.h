#pragma once
#include "wrapper.h"
#include "decimal.h"
#include "common.h"
#include <filesystem>
#include <memory>
#include <optional>


namespace quarkbot {


///Represents date
struct DateValue {
    int year = 0;
    int month = 0;
    int day = 0;
    bool operator == (const DateValue &) const = default;
    std::strong_ordering operator <=> (const DateValue &) const = default;
    constexpr bool valid() const {return month>0  && month <=12 && day > 0 && day <=31;}
    static DateValue from_string(std::string_view s) {
        DateValue out;
        if (std::sscanf(s.data(), "%d-%d-%d", &out.year, &out.month, &out.day) != 3) {
            throw std::runtime_error(std::string("'").append(s).append("' is not valid date. Use format YYYY-MM-DD"));
        }
        return out;
    }
};



///Represents time
struct TimeValue {
    int hour = 0;
    int minute = 0;
    int second = 0;
    bool operator == (const TimeValue &) const = default;
    std::strong_ordering operator <=> (const TimeValue &) const = default;
    static TimeValue from_string(std::string_view s) {
        TimeValue out;
        if (std::sscanf(s.data(), "%d:%d:%d", &out.hour, &out.minute, &out.second) != 3) {
            throw std::runtime_error(std::string("'").append(s).append("' is not valid time. Use format HH:MM:SS"));
        }
        return out;
    }
};



class IConfig {
public:
    virtual ~IConfig() = default;
    virtual std::shared_ptr<const IConfig> open_section(std::string_view name) const = 0;
    virtual std::optional<std::string_view> get_value(std::string_view name) const = 0;
    virtual std::optional<bool> get_value_bool(std::string_view name) const = 0;
    virtual std::optional<std::filesystem::path> get_path(std::string_view name) const = 0;
    virtual bool is_defined(std::string_view name) const = 0;
    virtual std::string_view get_section_path() const = 0;
    virtual std::vector<std::string_view> list_sections() const = 0;
    virtual std::vector<std::string_view> list_keys() const = 0;
class Null;
};


class IConfig::Null: public IConfig {
public:
    virtual std::shared_ptr<const IConfig> open_section(std::string_view ) const override;
    virtual std::optional<std::string_view> get_value(std::string_view ) const override {return {};}
    virtual std::optional<bool> get_value_bool(std::string_view ) const override {return {};}
    virtual std::optional<std::filesystem::path> get_path(std::string_view ) const override {return {};}
    virtual bool is_defined(std::string_view ) const override {return false;}
    virtual std::string_view get_section_path() const override {return "<empty configuration file>";}
    virtual std::vector<std::string_view> list_sections() const override {return {};}
    virtual std::vector<std::string_view> list_keys() const override {return {};}
};



///Holds configurations for various purposes
/**
 * The configuration is represented as key=value map in sections.
 * You can query a key or you can open a subsection. Each subsection is represented
 * as a new instance of Config class
 *
 * @code
 * Config sect = config["sect"];  //open subsection "sect"
 * Decimal v1 = sect["price"];    //load [sect]:price as Decimal
 * Decimal v2 = sect["amount] || 1.0_dec; //load [sect]:amount as Decimal, or 1 if not exists
 * sect["extra_param"] >> [&](Config cfg) { //execute lambda if section [extra_param] is defined
 *
 * };
 * @endcode
 *
 */
class Config: public Wrapper<IConfig> {
public:

    using Wrapper<IConfig>::Wrapper;

    ///Exception declaration
    class NotFound: public std::exception {
    public:
        NotFound(std::string_view path, std::string_view name)
            :path(path),name(name) {};

        const std::string &get_path() const {return path;}
        const std::string &get_name() const {return name;}
        virtual const char *what() const noexcept override {
            if (whatmsg.empty()) {
                whatmsg = "Mandatory configuration key is missing: " + path + ":" + name;
            }
            return whatmsg.c_str();
        }
    protected:
        std::string path;
        std::string name;
        mutable std::string whatmsg;
    };

    std::string_view get(std::string_view name, std::string_view defval) const {
        auto v = _ptr->get_value(name);
        if (v) return *v;
        return defval;
    }
    Decimal get(std::string_view name, Decimal defval) const {
        auto v = _ptr->get_value(name);
        if (v) return Decimal(*v);
        return defval;
    }
    DateValue get(std::string_view name, DateValue defval) const {
        auto v = _ptr->get_value(name);
        if (v) return DateValue::from_string(*v);
        return defval;
    }
    TimeValue get(std::string_view name, TimeValue defval) const {
        auto v = _ptr->get_value(name);
        if (v) return TimeValue::from_string(*v);
        return defval;
    }

    bool get(std::string_view name, bool defval) const {
        auto v = _ptr->get_value_bool(name);
        if (v) return *v;
        return defval;
    }

    template<typename T> requires(std::is_arithmetic_v<T>)
    T get(std::string_view name, T defval) const {
        auto v = _ptr->get_value(name);
        if (v) {
            std::from_chars(v->begin(),v->end(), defval);
        }
        return defval;
    }


    std::filesystem::path get(std::string_view name, std::filesystem::path defval) const {
        auto v = _ptr->get_path(name);
        if (v) return *v;
        return defval;
    }

    bool is_defined(std::string_view name) const {
        return _ptr->is_defined(name);
    }

    template<typename T>
    T get(std::string_view name) const {
        if constexpr(std::is_same_v<T, bool>) {
            auto v = _ptr->get_value_bool(name);
            if (v) return *v;
            throw NotFound(_ptr->get_section_path(), name);
        } else if constexpr(std::is_same_v<T, std::filesystem::path>) {
            auto v = _ptr->get_path(name);
            if (v) return *v;
            throw NotFound(_ptr->get_section_path(), name);
        } else if constexpr(std::is_same_v<T, DateValue>) {
            auto v = _ptr->get_value(name);
            if (v) return DateValue::from_string(*v);
            throw NotFound(_ptr->get_section_path(), name);
        } else if constexpr(std::is_same_v<T, TimeValue>) {
            auto v = _ptr->get_value(name);
            if (v) return TimeValue::from_string(*v);
            throw NotFound(_ptr->get_section_path(), name);
        }else if constexpr(std::is_constructible_v<T, std::string_view>) {
            auto v = _ptr->get_value(name);
            if (v) return T(*v);
            throw NotFound(_ptr->get_section_path(), name);
        } else if constexpr(std::is_arithmetic_v<T>) {
            auto v = _ptr->get_value(name);
            if (v) {
                Decimal dv(*v);
                return static_cast<T>(dv);
            }
            throw NotFound(_ptr->get_section_path(), name);
        } else if constexpr(std::is_convertible_v<T, Config>) {
            return get_section(name);
        } else {
            static_assert(assert_error<T>, "Can't convert a config value to T");
            throw;
        }
    }

    Config get_section(std::string_view name) const {
        return Config(_ptr->open_section(name));
    }

    struct ValueProxy;
    ValueProxy operator[](std::string_view s) const;

    ///Retrieve list of sections
    std::vector<std::string_view> list_sections() const {
        return _ptr->list_sections();
    }
    ///Retrieve list of keys
    std::vector<std::string_view> list_keys() const {
        return _ptr->list_keys();
    }

protected:


};

struct Config::ValueProxy {
    Config cfg;
    std::string_view name;
    template<typename T>
    operator T() const {
        return cfg.get<T>(name);
    }

    operator Config() const {
        return cfg.get_section(name);
    }
    ValueProxy operator[](std::string_view s) const {
        return {cfg.get_section(name), s};
    }
    ValueProxy operator[](const char *s) const {
        return {cfg.get_section(name), s};
    }
    template<typename T>
    auto operator || (T defval) const {
        return cfg.get(name, defval);
    }
    template<typename T>
    auto operator | (T defval) const {
        return cfg.get(name, defval);
    }

    template<std::invocable<ValueProxy> Fn>
    auto operator >> (Fn &&function) const { // @suppress("No return")
        using RetVal = std::invoke_result_t<Fn, ValueProxy>;
        if (cfg.is_defined(name)) {
            return function(*this);
        } else {
            if constexpr(!std::is_void_v<RetVal>) {
                return RetVal();
            }
        }
    }

};


inline Config::ValueProxy Config::operator[](std::string_view s) const {
    return {*this, s};
}

inline std::shared_ptr<const IConfig> IConfig::Null::open_section(std::string_view ) const {
    return Config().get_handle();
}




}
