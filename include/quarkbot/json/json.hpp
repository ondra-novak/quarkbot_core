#pragma once

#include "flatmap.hpp"
#include "utf8.hpp"
#include "flatmap.hpp"

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>
#include <string>
#include <format>
#include <optional>
#include <exception>
#include <charconv>
#include <algorithm>
#include <format>
#include <limits>


class Json ;

constexpr  bool is_digit(char c){return c>='0' && c <='9';};

class JsonNumber : public std::string {
public:
    
    JsonNumber() = default;
    JsonNumber(std::string_view text):std::string(is_valid_number(text)?text:std::string_view()) {}

    template<typename T>
    requires(std::is_integral_v<T> && std::is_arithmetic_v<T>)
    JsonNumber(T val):std::string(std::to_string(val)) {}
    template<typename T>
    requires(std::is_floating_point_v<T>)
    JsonNumber(T val):std::string(std::format("{:.12g}", val)) {}
    

    template<typename T>
    requires(std::is_integral_v<T> && std::is_arithmetic_v<T>)
    operator T() const {
        if (empty()) return static_cast<T>(0);
        if (find_first_of("eE") != npos) { //there can scientific number, in this case, use double conversion
                return static_cast<T>(std::strtod(this->c_str(), nullptr));
        } else if constexpr(std::is_unsigned_v<T>) {
                //strtoll would saturate at INT64_MAX for values above it
                return static_cast<T>(std::strtoull(this->c_str(), nullptr,10));
        } else {
                return static_cast<T>(std::strtoll(this->c_str(), nullptr,10));
        }
    }
    template<typename T>
    requires(std::is_floating_point_v<T>)
    operator T() const {
        if (empty()) return std::numeric_limits<T>::signaling_NaN();
        return static_cast<T>(std::strtod(this->c_str(), nullptr));
    }

    static bool is_valid_number(std::string_view text) {
        if (text.empty()) return false;
        std::size_t pos = 0;
        std::size_t tsz = text.size();

        // Optional minus sign
        if (text[pos] == '-') ++pos;
        if (pos >= tsz) return false;

        // Must have at least one digit before decimal point
        if (!is_digit(text[pos])) return false;

        // Leading zero must be alone
        if (text[pos] == '0' && pos + 1 < tsz && is_digit(text[pos + 1])) return false;

        // Consume digits
        while (pos < tsz && is_digit(text[pos])) ++pos;

        // Optional fractional part
        if (pos < tsz && text[pos] == '.') {
            ++pos;
            if (pos >= tsz || !is_digit(text[pos])) return false;
            while (pos < tsz && is_digit(text[pos])) ++pos;
        }

        // Optional exponent part
        if (pos < tsz && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            if (pos < tsz && (text[pos] == '+' || text[pos] == '-')) ++pos;
            if (pos >= tsz || !is_digit(text[pos])) return false;
            while (pos < tsz && is_digit(text[pos])) ++pos;
        }

        return pos == tsz;
    }

    template<typename Self>
    auto fields(this Self &self)  {
        using S = std::conditional_t<std::is_const_v<Self>, const std::string , std::string>;
        S & s = self;
        return std::tie(s);        
    }
};


using JsonTypes = std::variant<
    std::nullptr_t,
    std::string,
    JsonNumber,
    bool,
    std::vector<Json>,
    FlatMap<std::string, Json> >;

inline static std::string string_from_u8(std::u8string_view str) {
    std::string out(str.size(),0)    ;
    std::transform(str.begin(), str.end(), out.begin(), [](auto x){return static_cast<char>(x);});
    return out;
}

inline static std::string string_from_w(std::wstring_view str) {
    std::string out;
    Utf8<wchar_t>::to_utf8(str.begin(), str.end(), std::back_inserter(out));
    return out;
}

class JsonBuilder {
public:
    JsonBuilder();
    JsonBuilder(std::initializer_list<JsonBuilder> list);

    template<typename T>    
    JsonBuilder(const T &other);

    Json build() const;

protected:

    enum class Type {
        string,
        value,
        array
    };
    struct String {
        const char *str;
        std::size_t len;
    };

    struct SingleItem {
        const void *ptr;
        Json (*construct)(const void *ptr);
    };

    struct List {
        std::initializer_list<JsonBuilder>::iterator begin;
        std::initializer_list<JsonBuilder>::iterator end;
    };

    Type type;
    union {
        String string;
        SingleItem item;
        List list;
    };

    Json build_object() const;
    Json build_array() const;

};


class Json: public JsonTypes  {
public:
    template<typename T> static constexpr bool failed = false;

    using JsonTypes::JsonTypes;
    Json() {};
    Json(std::string_view str):JsonTypes(std::string(str)) {}
    Json(std::u8string_view str):JsonTypes(std::string(string_from_u8(str))) {}
    Json(const std::u8string &str):Json(std::u8string_view(str)) {}
    Json(std::wstring_view str):Json(string_from_w(str)) {}
    Json(const std::wstring &str):Json(string_from_w(str)) {}
    Json(bool b):JsonTypes(b) {}
    
    template<typename T>
    requires(std::is_arithmetic_v<T>) 
    Json(T val):JsonTypes(JsonNumber(val)) {}

    using Object = FlatMap<std::string, Json> ;
    using Array = std::vector<Json>;

    static const Json &empty_json() {
        static Json e;
        return e;
    }

    bool is_null() const {return std::holds_alternative<std::nullptr_t>(*this);}
    bool is_bool() const {return std::holds_alternative<bool>(*this);}
    bool is_number() const {return std::holds_alternative<JsonNumber>(*this);}
    bool is_string() const {return std::holds_alternative<std::string>(*this);}    
    bool is_array() const {return std::holds_alternative<Array>(*this);}
    bool is_object() const {return std::holds_alternative<Object>(*this);}

    template<typename T>
    T as() const {
        if constexpr(std::is_same_v<T, bool>) {
            if (is_bool()) {
                return std::get<bool>(*this);
            } else if (is_number()) {
                auto v = static_cast<int>(std::get<JsonNumber>(*this));
                return v != 0;            
            } else if (is_string()) {
                return std::get<std::string>(*this) == "true";
            } else {
                return false;
            }

        } else if constexpr(std::is_arithmetic_v<T>) {
            if (is_bool()) {
                return static_cast<T>(std::get<bool>(*this)?1:0);
            } else if (is_number()) {
                return static_cast<T>(std::get<JsonNumber>(*this));                
            } else if (is_string()) {
                return static_cast<T>(JsonNumber(std::get<std::string>(*this)));
            } else {
                return T();
            }
        } else if constexpr(std::is_constructible_v<T, std::string_view>) {
            if (is_bool()) {
                return T(std::string_view(std::get<bool>(*this)?"true":"false"));
            } else if (is_number()) {
                return T(std::get<JsonNumber>(*this));             
            } else if (is_string()) {                
                return std::get<std::string>(*this);
            } else {
                return T();
            }
        } else if constexpr(std::is_constructible_v<T, std::u8string_view>) {
            if (is_bool()) {
                return T(std::u8string_view(std::get<bool>(*this)?u8"true":u8"false"));
            } else if (is_number()) {
                auto s = std::get<JsonNumber>(*this);
                return T(std::u8string_view(reinterpret_cast<const char8_t *>(s.data()),s.size()));             
            } else if (is_string()) {
                const auto &s = std::get<std::string>(*this);;                
                return T(std::u8string_view(reinterpret_cast<const char8_t *>(s.data()),s.size()));
            } else {
                return T();
            }
        } else if constexpr(std::is_same_v<T, std::wstring>) {
            auto s = this->as<std::string_view>();
            std::wstring out;
            Utf8<wchar_t>::from_utf8(s.begin(), s.end(),std::back_inserter(out));
            return out;
        } else if constexpr(std::is_same_v<T, std::u8string_view>) {
            auto s = this->as<std::string_view>();
            return std::u8string_view(reinterpret_cast<const char8_t *>(s.data()), s.size());
        }  else {
            static_assert(failed<T>, "Can't convert");
        }
    }

    template<typename T>
    auto as_vector() const {
        std::vector<T> out;
        const auto &a = as_array();
        out.reserve(a.size());
        for (const auto &x: a) out.push_back(x.as<T>());
        return out;
    }


    auto as_bool() const {return as<bool>();}
    auto as_int() const {return as<int>();}
    auto as_unsigned_int() const {return as<unsigned int>();}
    auto as_long() const {return as<long>();}
    auto as_unsigned_long() const {return as<unsigned long>();}
    auto as_float() const {return as<float>();}
    auto as_double() const {return as<double>();}
    auto as_text() const {return as<std::string_view>();}
    auto as_wtext() const {return as<std::wstring>();}
    auto as_utf8() const {return as<std::u8string_view>();}
    const JsonNumber &as_number() const {
        static JsonNumber empty;
        if (is_number()) return get<JsonNumber>(*this);
        else return empty;
    }

    const Array &as_array() const {
        if (is_array()) return std::get<Array>(*this);
        else {
            static Array empty;
            return empty;
        }
    }
    const Object &as_object() const {
        if (is_object()) return std::get<Object>(*this);
        else {
            static Object empty;
            return empty;
        }
    }

    const Json &operator[](std::size_t index) const {
        auto &x = as_array();
        if (index >= x.size()) return empty_json();
        return x[index];
    }

    const Json &operator[](std::string_view key) const {
        auto &x = as_object();
        auto iter = x.find(key);  
        if (iter == x.end()) return empty_json();
        return iter->second;
    }
    
    template<std::invocable<Array &> Fn>
    auto update(Fn &&fn) {
        if (!is_array()) *this = Array();
        return fn(std::get<Array>(*this));
    }
    template<std::invocable<Object &> Fn>
    auto update(Fn &&fn) {
        if (!is_object()) *this = Object();
        return fn(std::get<Object>(*this));
    }

    auto push_back(Json &&val) {
        return update([&](Array &x){
            return x.push_back(std::move(val));
        });
    }

    auto push_back(const Json &val) {
        return update([&](Array &x){
            return x.push_back(val);
        });
    }

    auto set(std::string key, Json &&value) {
        return update([&](Object &x){
            return x[std::move(key)] = std::move(value);
        });
    }

    auto set(std::string key, const Json &value) {
        return update([&](Object &x){
            return x[std::move(key)] = value;
        });
    }

    auto set(std::initializer_list<std::pair<std::string_view, JsonBuilder> > items) {
        return update([&](Object &i){
            for (auto x: items) {
                i[x.first] = x.second.build();
            };            
        });
    }

    template<std::invocable<char> Fn>
    void serialize(Fn &&fn) const  {
        if (is_null()) write_token(fn,"null");
        else if (is_bool()) write_token(fn, as_bool()?"true":"false");
        else if (is_string()) write_string(fn, as_text());
        else if (is_number()) {
            auto n = as_number();
            write_token(fn, n);
        } else if (is_array()) {
            auto &a = as_array();
            fn('[');
            auto iter = a.begin();
            auto end = a.end();
            if (iter != end) {
                iter->serialize(fn);
                ++iter;
                while (iter != end) {
                    fn(',');
                    iter->serialize(fn);
                    ++iter;
                }
            }
            fn(']');
        } else if (is_object()) {
            auto &o = as_object();
            fn('{');
            auto iter = o.begin();
            auto end = o.end();
            if (iter != end) {
                write_string(fn, iter->first);
                fn(':');
                iter->second.serialize(fn);                
                ++iter;
                while (iter != end) {
                    fn(',');
                    write_string(fn, iter->first);
                    fn(':');
                    iter->second.serialize(fn);                
                    ++iter;
                }
            }
            fn('}');            
        }
    }

    class ParseError: public std::exception {
    public:
        virtual const char *what() const noexcept {return "json parse error";}
    };


    template<std::invocable<> Fn>
    requires(std::is_invocable_r_v<std::optional<char>, Fn>)
    static Json parse(Fn &&fn) {     
        char c= read_skip_ws(fn);
        return parse_first_chr(c, fn);
    }

    Json(std::initializer_list<JsonBuilder> list):Json(JsonBuilder(list).build()) {}

    std::string to_string() const {
        std::vector<char> buff;
        serialize([&](char c){buff.push_back(c);});
        return {buff.begin(), buff.end()};
    }

    static Json from_string(std::string_view text) {
        return parse([&,i=std::size_t(0)]()mutable -> std::optional<char>{
            if (i >= text.length()) return std::nullopt;
            else return text[i++];
        });        
    }

protected:

    using ReadChr = std::optional<char>;

    template<typename Fn>
    static void write_token(Fn &&fn, std::string_view s) {
        for (auto x: s) fn(x);
    }
    template<typename Fn>
    static void write_string(Fn &&fn, std::string_view s) {
        fn('"');
        for (auto x: s) {
            switch (x) {
                case '\n': write_token(fn,"\\n");break;
                case '\r': write_token(fn,"\\r");break;
                case '\t': write_token(fn,"\\t");break;
                case '\f': write_token(fn,"\\f");break;
                case '\b': write_token(fn,"\\b");break;
                case '\\': write_token(fn,"\\\\");break;
                case '\"': write_token(fn,"\\\"");break;
                default: 
                    if (x >= 0 && x < 32) {
                        char buff[6] = "\\u";
                        auto iter = std::format_to_n(buff+2,4,"{:04x}", x);
                        write_token(fn,{buff,iter.out});
                    } else {
                        fn(x);
                    }
            }
        }
        fn('"');
    }
    template<typename Fn>
    static char read_skip_ws(Fn &&fn) {
        ReadChr c = fn();
        while (c && std::isspace(*c)) {
            c = fn();
        }
        if (!c) throw ParseError();
        return *c;
    }
    template<typename Fn>
    static Json parse_first_chr(char &c, Fn &&fn) {
        switch (c) {
            case 't': check(c, fn, "true"); c = 0; return Json(true);
            case 'f': check(c, fn, "false"); c = 0; return Json(false);
            case 'n': check(c, fn, "null"); c = 0; return Json(nullptr);
            case '"': c =0; return Json(parse_string(fn));
            case '[':  {
                Array arr;
                c = read_skip_ws(fn);
                if (c != ']') {
                    arr.push_back(parse_first_chr(c,fn));
                    if (!c) c = read_skip_ws(fn);
                    while (c != ']') {
                        if (c != ',') throw ParseError();
                        c = read_skip_ws(fn);
                        arr.push_back(parse_first_chr(c, fn));
                        if (!c) c = read_skip_ws(fn);
                    }
                }
                c = 0;
                return Json(std::move(arr));                
            }
            case '{': {
                Object obj;
                c = read_skip_ws(fn);
                if (c != '}') {
                    if (c!='"') throw ParseError();
                    while (true) {
                        std::string k = parse_string(fn);
                        c = read_skip_ws(fn);
                        if (c!=':') throw ParseError();
                        c = read_skip_ws(fn);
                        auto v = parse_first_chr(c, fn);
                        if (!c) c = read_skip_ws(fn);
                        if (!obj.emplace(std::move(k), std::move(v)).second) throw ParseError();
                        if (c == ',') {
                            c = read_skip_ws(fn);
                            continue;
                        } else if (c != '}') {
                            throw ParseError();
                        } else {
                            break;
                        }
                    }
                }
                c = 0;
                return Json(std::move(obj));
            }
            default:
                return Json(parse_number(c, fn));
        }
    }

    template<typename Fn>
    static void check(char c, Fn &&fn, std::string_view token) {
        if (c != token[0]) throw ParseError();
        for (auto &x: token.substr(1)) {
            auto cc = fn();
            if (!cc || *cc != x) throw ParseError();            
        }
    }
    template<typename Fn>
    static JsonNumber parse_number(char &c, Fn &&fn) {
        std::string buff;
        while (is_digit(c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
            buff.push_back(c);
            auto cc = fn();
            c = !cc?' ': *cc;
        }
        double v;
        auto st = std::from_chars(buff.data(), buff.data()+buff.size(), v);
        if (st.ec != std::errc{} || st.ptr != buff.data()+buff.size()) throw ParseError();
        if (std::isspace(c)) c = 0;
        //Keep the original token rather than the double. Storing the double would
        //round-trip through JsonNumber's {:.12g} formatting and silently truncate
        //anything above 12 significant digits - a 13-digit unix millisecond
        //timestamp, for instance.
        //
        //Only for tokens that are valid JSON numbers, though: the loop above also
        //accepts shapes JSON forbids and is_valid_number rejects ("05", "007",
        //"00", ".5", "5."). Those still go through the double, because serialize()
        //emits a number's stored token verbatim - keeping them as-is would make
        //this parser re-emit literals a strict peer's JSON.parse would reject.
        //The valid token is assigned to the base string directly, since
        //JsonNumber's string_view constructor would re-validate redundantly.
        JsonNumber out;
        if (JsonNumber::is_valid_number(buff)) {
            static_cast<std::string &>(out) = std::move(buff);
        } else {
            out = JsonNumber(v);
        }
        return out;
    }
    template<typename Fn>
    static std::string parse_string(Fn &&fn) {        
        std::basic_string<char16_t> data;
        std::string outdata;
        char hexbuf[4];
        int m = 0;
        auto cc = fn();        
                
        while (cc && (*cc!='"' || m)) {
            unsigned char c = static_cast<unsigned char>(*cc);
            if (m == 0) {
                if (c == '\\') m = -1;
                else {
                    if (!data.empty()) {
                        Utf8<char16_t>::to_utf8(data.begin(), data.end(), std::back_inserter(outdata));                
                        data.clear();
                    }
                    outdata.push_back(static_cast<char>(c));
                }
            } else if (m == -1) {
                m = 0;
                switch (c) {
                    case 'n': data.push_back('\n');break;
                    case 'r': data.push_back('\r');break;
                    case 't': data.push_back('\t');break;
                    case 'f': data.push_back('\f');break;
                    case 'a': data.push_back('\a');break;
                    case 'u': m = 1; break;
                    default: data.push_back(c);
                }
            } else if (m > 0) {
                if (!std::isxdigit(c)) throw ParseError();
                hexbuf[m-1] = static_cast<char>(c);
                if (m == 4) {
                    std::uint16_t codepoint = 0;
                    std::from_chars(hexbuf,hexbuf+4, codepoint, 16);                    
                    data.push_back(codepoint);
                    m = 0;
                } else {
                    ++m;
                }                
            }
             cc = fn();
        }
        if (!data.empty()) {
            Utf8<char16_t>::to_utf8(data.begin(), data.end(), std::back_inserter(outdata));                
        }
        return outdata;
    }
};

inline JsonBuilder::JsonBuilder():JsonBuilder(std::initializer_list<JsonBuilder>{}) {}
inline JsonBuilder::JsonBuilder(std::initializer_list<JsonBuilder> list)
    :type(Type::array),list(List{list.begin(), list.end()}) {}

template<typename T>    
inline JsonBuilder::JsonBuilder(const T &other):type(Type::value) {
    if constexpr(std::is_constructible_v<std::string_view, T>) {
        std::string_view txt(other);
        type = Type::string;
        string.str = txt.data();
        string.len = txt.size();
    } else {    
        static_assert(std::is_constructible_v<Json, const T &>, "Json object is not constructible from this type");
        item.ptr = &other;
        item.construct = [](const void *src) {
            return Json(*static_cast<const T *>(src));
        };
    }
}

inline Json JsonBuilder::build_object() const {
    Json::Object::Super out;
    out.reserve(static_cast<std::size_t>(std::distance(list.begin, list.end)));
    for (auto iter = list.begin; iter != list.end; ++iter) {
        auto k = iter->list.begin;
        auto v = k;
        std::advance(v,1);
        out.push_back({std::string(k->string.str, k->string.len), v->build()});
    }
    return Json::Object(std::move(out));

}
inline Json JsonBuilder::build_array() const {
    Json::Array out;
    out.reserve(static_cast<std::size_t>(std::distance(list.begin, list.end)));
    for (auto iter = list.begin; iter != list.end; ++iter) {
        out.push_back(iter->build());
    }
    return Json(std::move(out));
}
inline Json JsonBuilder::build() const {


    switch (type) {
        case Type::string: return Json(std::string(string.str, string.len));
        case Type::value: return Json(item.construct(item.ptr));
        case Type::array:  
                if ( std::all_of(list.begin, list.end, [](const JsonBuilder &x){
                    if (x.type != Type::array) return false;
                    auto sz = std::distance(x.list.begin, x.list.end);
                    return sz == 2 && x.list.begin->type == Type::string;
                })) {
                    return build_object();
                } else {
                    return build_array();
                }
        default: return Json();            
        
    }
}

