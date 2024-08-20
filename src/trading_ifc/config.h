#pragma once
#include "decimal.h"
#include "timer.h"
#include <variant>
#include <span>
#include <vector>
#include <map>
#include <string>
#include <charconv>
namespace trading_api {

struct DateValue {
    int year = 0;
    int month = 0;
    int day = 0;
    bool operator == (const DateValue &) const = default;
    std::strong_ordering operator <=> (const DateValue &) const = default;
    constexpr bool valid() const {return month>0  && month <=12 && day > 0 && day <=31;}
};



struct TimeValue {
    int hour = 0;
    int minute = 0;
    int second = 0;
    bool operator == (const TimeValue &) const = default;
    std::strong_ordering operator <=> (const TimeValue &) const = default;
};


class CfgValue : public std::string_view{
public:
    using std::string_view::string_view;
    CfgValue() = default;
    CfgValue(const std::string_view &other):std::string_view(other) {}

    operator Decimal() const {return Decimal(*this);}
    template<typename T> requires(std::is_integral_v<T>)
    operator T() const {
        T out = {};
        std::from_chars(data(), data()+size(),&out);
        return out;
    }
    template<typename T> requires(std::is_floating_point_v<T>)
    operator T() const {
        return std::strtod(data(), nullptr);
    }
    operator DateValue() const {
        DateValue out = {};
        sscanf(data(), "%d-%d-%d", &out.year, &out.month, &out.day);
        return out;
    }
    operator TimeValue() const {
        TimeValue out = {};
        sscanf(data(), "%d:%d:%d", &out.hour, &out.minute, &out.second);
        return out;
    }
    operator Timestamp() const {
        std::tm tnfo;
        sscanf(data(), "%d-%d-%d%*c%d:%d:%d",
                &tnfo.tm_year,&tnfo.tm_mon,&tnfo.tm_mday,
                &tnfo.tm_hour,&tnfo.tm_min,&tnfo.tm_sec);
        tnfo.tm_year-=1900;
        tnfo.tm_mon-=1;
        return std::chrono::system_clock::from_time_t(std::mktime(&tnfo));
    }
    operator std::string() const {
        return std::string(*this);
    }
    operator bool() const {
        if (*this == "false" || *this == "0" || *this == "") return false;
        return true;
    }
    template<typename T>
    T operator()(const T &def) const {
        if (*this == "") return def;
        else return *this;
    }

};

class Config: public std::unordered_map<std::string, std::string> {
public:
    CfgValue operator[](std::string name) const {
        auto iter = this->find(name);
        if (iter == this->end()) return {};
        else return CfgValue(iter->second);
    }
    bool defined(std::string name) const {
        auto iter = this->find(name);
        return iter != this->end();
    }
};



}
