#include "check.h"
#include "quarkbot/serializer/serialize.hpp"
#include "quarkbot/serializer/serialize_schema_to_json.hpp"
#include "quarkbot/json/json.hpp"
#include "quarkbot/types.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>

using namespace srl;

template<typename T>
void test_schema(Json result) {
    srl::BinarySchemaGenerator gen;
    gen.build_schema<T>();
    Json schema = serialize_schema_to_json(gen.get_schema());
    CHECK_EQUAL(schema.to_string(), result.to_string());
}

struct Inner {
    double foo;
    unsigned int bar;

    void serialize(this auto &self, auto &arch) {
        arch(self.foo,"foo");
        arch(self.bar,"bar");
    }
    constexpr bool operator==(const Inner &) const = default;
};

using Variant = std::variant<std::monostate, std::string, int, double>;
template<> constexpr std::string_view srl::schema_type_name<std::monostate> = "Monostate";
template<> constexpr std::string_view srl::schema_type_name<Variant> = "Variant";

static_assert(srl::HasSerializeRule<FlatMap<std::string, Json> >);

void serialize_rule(const Json &x, auto &ar) {
    const JsonTypes &t = x;
    ar(t);
}
void serialize_rule(Json &x, auto &ar) {
    JsonTypes &t = x;
    ar(t);
}

void test_schemas() {

    test_schema<Json>(Json {});

    test_schema<unsigned int>(Json{{"root", "unsigned_varint"},{"types", Json::Object()}});
    test_schema<Inner>(Json{
        {"root",schema_type_name<Inner>},
        {"types",{
            {schema_type_name<Inner>, {
                {"fields",Json::Array {
                    {"foo","double"},
                    {"bar", "unsigned_varint"},
                    }
                },
                {"layout","sequence"}
            }}
        }
    }});
    using Array = std::vector<bool>;
    test_schema<Array>(Json{
        {"root",schema_type_name<Array>},
        {"types", {
            {schema_type_name<Array>, {
                {"fields", {"bool"}},
                {"layout","array"}
            }}
        }}
    });
    using Map = std::unordered_map<std::string, int>;
    test_schema<Map>(Json{
        {"root",schema_type_name<Map>},
        {"types", {
            {schema_type_name<Map>, {
                {"fields", {"string","signed_varint"}},
                {"layout","map"}
            }}
        }}
    });   
    test_schema<Variant>(Json{
        {"root","Variant"},
        {"types", {
            {"Monostate",{
                {"layout","empty"},
                {"size",0}
            }},
            {"Variant", {
                {"fields", {"Monostate","string","signed_varint","double"}},
                {"layout","variant"}
            }}
        }}
    });
    using Optional = std::optional<std::string>;
    test_schema<Optional>(Json{
        {"root",schema_type_name<Optional>},
        {"types", {
            {schema_type_name<Optional>,{
                {"fields", {"string"}},
                {"layout","optional"}
            }}
        }}
    });
    using Binary = std::array<char, 10>;
    test_schema<Binary>(Json {
        {"root", schema_type_name<Binary>},
        {"types", {
            {schema_type_name<Binary>, {
                {"layout","blob"},
                {"size",10}
            }}
        }}
    });
    using Shared = std::shared_ptr<int>;
    test_schema<Shared>(Json {
    {"root", schema_type_name<Shared>},
    {"types", {
        {schema_type_name<Shared>, {
            {"fields",{"signed_varint"}},
                {"layout","shared"}
            }}
        }}
    });

    using Unique = std::unique_ptr<int>;
    test_schema<Unique>(Json {
    {"root", schema_type_name<Unique>},
    {"types", {
            {schema_type_name<Unique>, {
                {"fields",{"signed_varint"}},
                {"layout","optional"}            
            }},
        }}
    });   
}

template<typename T>
struct CheckResult {
    bool operator()(const T &a, const T &b) const {
        return a==b;
    }
};

template<typename T>
struct CheckResult<std::unique_ptr<T> >  {
    bool operator()(const std::unique_ptr<T> &a, const std::unique_ptr<T> &b) const{
    return *a==*b;
    }
};

template<typename T>
struct CheckResult<std::shared_ptr<T> >  {
    bool operator()(const std::shared_ptr<T> &a, const std::shared_ptr<T> &b) const{
    return *a==*b;
    }
};

template<typename T>
struct CheckResult<std::vector<std::shared_ptr<T> > >  {
    bool operator()(const std::vector<std::shared_ptr<T> > &a, const std::vector<std::shared_ptr<T> > &b) const{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i] && (!a[i] || !b[i] || *(a[i]) != *(b[i]))) return false;
    }
    return true;
}
};


template<typename T>
void test_serialize(T val, std::size_t expected_size) {
    std::vector<std::uint8_t> s;
    bool stream_overflor = false;
    auto wr = [&](std::span<const std::uint8_t> c) {s.insert(s.end(), c.begin(), c.end());};
    auto rd = [&, pos = std::size_t(0)](std::span<std::uint8_t> c) mutable {
        if (c.size() + pos > s.size()) {
            CHECK(stream_overflor);
        } else {
            std::copy_n(s.begin()+static_cast<std::ptrdiff_t>(pos), c.size(), c.begin());
            pos += c.size();
        }
    };

    {
        BinarySerializer binwr(wr);
        binwr(val);
    }

    T out;
    {
        BinaryParser binrd(rd);
        binrd(out);
    }
    CheckResult<T> check;
    CHECK(check(val,out));
    CHECK_EQUAL(s.size(), expected_size);
}

void test_serializers() {
    test_serialize(42,1);
    test_serialize<Inner>({3.14, 1234},10);
    test_serialize<std::string>("hello world",12);
    test_serialize<Variant>("hello world",13);
    test_serialize<Variant>(3.1415,9);
    test_serialize<Variant>(std::monostate{},1);
    test_serialize<std::monostate>({},0);
    test_serialize<quarkbot::Fill>({},61);
    test_serialize<std::vector<int> >({1,2,3,4}, 5);
    test_serialize<std::map<std::string, int> >({{"aaa",1},{"bbb",2}}, 11);
    test_serialize<std::optional<float> > (3.14,5);
    test_serialize<std::optional<float> > (std::nullopt,1);
    test_serialize<std::unique_ptr<std::string> >(std::make_unique<std::string>("Ahoj lidi"),11);    
    auto shared = std::make_shared<std::string>("Shared");
    std::vector<std::shared_ptr<std::string> > test_shared({shared,shared,nullptr});
    test_serialize(test_shared, 11);
}


int main() {
    test_schemas();
    test_serializers();

/*    test_schema();
    test_fill();
    test_optional();
    test_map();
    test_array();*/

}
