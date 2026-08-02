#include "check.h"

#include "quarkbot/serializer/serialize.hpp"
#include "quarkbot/serializer/serialize_schema_to_json.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

using Bytes = std::vector<std::uint8_t>;

///serialize into a byte vector - constexpr usable for literal types
template<typename T>
constexpr Bytes encode(const T &val) {
    Bytes out;
    srl::serialize_to<std::uint8_t>(val, std::back_inserter(out));
    return out;
}

template<typename T>
constexpr T decode(const Bytes &data) {
    T out = {};
    srl::deserialize_from(data.begin(), data.end(), out);
    return out;
}

///encode, decode, compare - the property every rule must hold
template<typename T>
constexpr bool roundtrip(const T &val) {
    return decode<T>(encode(val)) == val;
}

///encoded size, so tests can pin the wire format down
template<typename T>
constexpr std::size_t encoded_size(const T &val) {
    return encode(val).size();
}

///compares an encoding against an expected byte sequence
constexpr bool same_bytes(const Bytes &a, std::initializer_list<std::uint8_t> b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

// ---------------------------------------------------------------------------
// test types
// ---------------------------------------------------------------------------

struct Inner {
    int foo = {};
    double bar = {};
    constexpr bool operator==(const Inner &) const = default;
    constexpr void serialize(this auto &self, auto &ar) {
        ar(self.foo, "foo");
        ar(self.bar, "bar");
    }
};

struct Outer {
    Inner inner = {};
    std::string name = {};
    std::vector<int> items = {};
    constexpr bool operator==(const Outer &) const = default;
    constexpr void serialize(this auto &self, auto &ar) {
        ar(self.inner, "inner");
        ar(self.name, "name");
        ar(self.items, "items");
    }
};

///rule provided through ADL as a plain (non-template) overload
namespace ext {

struct Celsius {
    int deci = {};      //tenths of a degree
    constexpr bool operator==(const Celsius &) const = default;
};

///stores only the whole degrees, so the test can observe that this rule really ran
struct CelsiusRule {
    using value_type = Celsius;
    static constexpr std::size_t field_count() {return 1;}
    static constexpr void iterate_fields(auto &&cb) {cb(std::type_identity<int>{});}
    static constexpr void init_layout(srl::LayoutBase &l) {
        l.type = srl::LayoutType::sequence;
        l.fields[0].type_name = srl::type_name<int>;
        l.fields[0].field_name = "degrees";
    }
    static constexpr void serialize(const Celsius &s, auto &&cb) {
        int whole = s.deci / 10;
        cb(whole);
    }
    static constexpr void deserialize(Celsius &s, auto &&cb) {
        int whole = {};
        cb(whole);
        s.deci = whole * 10;
    }
};

constexpr auto get_serialize_rule(std::type_identity<Celsius>) {return CelsiusRule{};}

}

///rule provided through ADL as a *constrained template*. Before the customization
///point was introduced this collided with the built-in trivially-copyable rule and
///the collision silently reported "no rule exists".
namespace ext {

struct Tagged {
    constexpr bool operator==(const Tagged &) const = default;
};

template<typename T> concept IsTagged = std::is_base_of_v<Tagged, T>;

template<typename T>
struct TaggedRule {
    using value_type = T;
    static constexpr std::size_t field_count() {return 1;}
    static constexpr void iterate_fields(auto &&cb) {cb(std::type_identity<int>{});}
    static constexpr void init_layout(srl::LayoutBase &l) {
        l.type = srl::LayoutType::sequence;
        l.fields[0].type_name = srl::type_name<int>;
    }
    static constexpr void serialize(const T &s, auto &&cb) {cb(s.value);}
    static constexpr void deserialize(T &s, auto &&cb) {cb(s.value);}
};

struct Counter: Tagged {
    int value = {};
    constexpr bool operator==(const Counter &) const = default;
};

template<IsTagged T>
constexpr auto get_serialize_rule(std::type_identity<T>) {return TaggedRule<T>{};}

}

///a type in the global namespace, so ADL can never reach a rule for it
struct Foreign {
    int a = {};
    int b = {};
    constexpr bool operator==(const Foreign &) const = default;
};

template<>
struct srl::custom_serialize_rule<Foreign> {
    using value_type = Foreign;
    static constexpr std::size_t field_count() {return 2;}
    static constexpr void iterate_fields(auto &&cb) {
        cb(std::type_identity<int>{});
        cb(std::type_identity<int>{});
    }
    static constexpr void init_layout(srl::LayoutBase &l) {
        l.type = srl::LayoutType::sequence;
        l.fields[0].type_name = srl::type_name<int>;
        l.fields[0].field_name = "a";
        l.fields[1].type_name = srl::type_name<int>;
        l.fields[1].field_name = "b";
    }
    static constexpr void serialize(const Foreign &s, auto &&cb) {cb(s.a); cb(s.b);}
    static constexpr void deserialize(Foreign &s, auto &&cb) {cb(s.a); cb(s.b);}
};

///no rule at all - not trivially copyable, no serialize method
struct NoRule {
    virtual ~NoRule() = default;
    std::vector<int> data;
};

// ---------------------------------------------------------------------------
// 1. rule dispatch - which built-in rule does each category resolve to
// ---------------------------------------------------------------------------

template<typename T>
using rule_of = decltype(srl::get_rule(std::type_identity<T>{}));

static_assert(std::is_same_v<rule_of<unsigned int>,   srl::SerializeRuleUnsignedNumber<unsigned int> >);
static_assert(std::is_same_v<rule_of<int>,            srl::SerializeRuleSignedNumber<int> >);
static_assert(std::is_same_v<rule_of<double>,         srl::SerializeRuleTrivial<double> >);
static_assert(std::is_same_v<rule_of<Inner>,          srl::SerializeRuleWithMethod<Inner> >);
static_assert(std::is_same_v<rule_of<std::string>,    srl::SerializeRuleString<char> >);
static_assert(std::is_same_v<rule_of<std::vector<int> >, srl::SerializeRuleLinearContainer<std::vector<int> > >);
static_assert(std::is_same_v<rule_of<std::tuple<int,bool> >, srl::SerializeRuleTupleLike<std::tuple<int,bool> > >);

using TestVariant = std::variant<int, std::string>;
static_assert(std::is_same_v<rule_of<TestVariant>, srl::SerializeRuleVariantLike<TestVariant> >);

//regression: IsMap used to name a nonexistent member, so every map fell through to IsSet
using TestMap = std::map<std::string, int>;
static_assert(srl::IsMap<TestMap>);
static_assert(!srl::IsSet<TestMap>);
static_assert(std::is_same_v<rule_of<TestMap>, srl::SerializeRuleMap<TestMap> >);
static_assert(std::is_same_v<rule_of<std::set<int> >, srl::SerializeRuleSet<std::set<int> > >);

//regression: optional<trivial> is itself trivially copyable and used to be
//encoded as an opaque blob (padding included) instead of <has_value><value>
static_assert(std::is_same_v<rule_of<std::optional<int> >, srl::SerializeRuleOptional<std::optional<int> > >);
static_assert(std::is_same_v<rule_of<std::optional<std::string> >, srl::SerializeRuleOptional<std::optional<std::string> > >);

// ---------------------------------------------------------------------------
// 2. the customization point
// ---------------------------------------------------------------------------

//a user rule found through ADL wins over any built-in one
static_assert(std::is_same_v<rule_of<ext::Celsius>, ext::CelsiusRule>);
static_assert(std::is_same_v<rule_of<ext::Counter>, ext::TaggedRule<ext::Counter> >);
//...and the explicit specialization covers what ADL cannot reach
static_assert(std::is_same_v<rule_of<Foreign>, srl::custom_serialize_rule<Foreign> >);

static_assert(srl::SerializeRuleExists<ext::Celsius>);
static_assert(srl::SerializeRuleExists<ext::Counter>);
static_assert(srl::SerializeRuleExists<Foreign>);
static_assert(srl::SerializeRuleExists<Outer>);

//a type with no rule reports cleanly instead of hard-erroring or falling into a blob
static_assert(!srl::SerializeRuleExists<NoRule>);

//usable before any concrete rule is named
static_assert(srl::type_has_zero_fields<int>);
static_assert(!srl::type_has_zero_fields<Inner>);

// ---------------------------------------------------------------------------
// 3. constexpr round trips
// ---------------------------------------------------------------------------
// Everything except std::map / std::set is a literal type, so the round trip
// property can be proven at compile time.

static_assert(roundtrip(true));
static_assert(roundtrip(false));
static_assert(roundtrip<std::uint8_t>(255));
static_assert(roundtrip<std::uint64_t>(0));
static_assert(roundtrip<std::uint64_t>(246));
static_assert(roundtrip<std::uint64_t>(247));
static_assert(roundtrip<std::uint64_t>(0xFFFFFFFFFFFFFFFFULL));
static_assert(roundtrip<std::int64_t>(-1));
static_assert(roundtrip<std::int64_t>(std::numeric_limits<std::int64_t>::min()));
static_assert(roundtrip<std::int64_t>(std::numeric_limits<std::int64_t>::max()));
static_assert(roundtrip(3.141592653589793));
static_assert(roundtrip(-0.5f));

static_assert(roundtrip(std::string{}));
static_assert(roundtrip(std::string("hello world")));
static_assert(roundtrip(std::vector<int>{}));
static_assert(roundtrip(std::vector<int>{1, -2, 3000}));
static_assert(roundtrip(std::optional<int>{}));
static_assert(roundtrip(std::optional<int>{42}));
static_assert(roundtrip(std::optional<std::string>{"x"}));
static_assert(roundtrip(std::tuple<int, bool, double>{7, true, 1.5}));
static_assert(roundtrip(TestVariant{123}));
static_assert(roundtrip(TestVariant{std::string("abc")}));

static_assert(roundtrip(Inner{42, 2.5}));
static_assert(roundtrip(Outer{{1, 0.25}, "name", {1, 2, 3}}));
static_assert(roundtrip(std::vector<Inner>{{1, 1.0}, {2, 2.0}}));
//built element by element: an initializer_list of non-literal elements cannot
//appear in a constant expression, but the container itself is fine
constexpr std::optional<std::vector<std::string> > make_nested() {
    std::vector<std::string> v;
    v.push_back("a");
    v.push_back("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");   //past SSO
    return v;
}
static_assert(roundtrip(make_nested()));

//custom rules round trip too - Celsius deliberately loses the tenths
static_assert(decode<ext::Celsius>(encode(ext::Celsius{235})) == ext::Celsius{230});
static_assert(roundtrip(ext::Counter{{}, 99}));
static_assert(roundtrip(Foreign{5, 300000}));

// ---------------------------------------------------------------------------
// 4. wire format
// ---------------------------------------------------------------------------
// varuint: 0..246 inline, otherwise (246 + byte count) followed by big endian bytes
static_assert(same_bytes(encode(0u), {0x00}));
static_assert(same_bytes(encode(246u), {0xF6}));
static_assert(same_bytes(encode(247u), {0xF7, 0xF7}));
static_assert(same_bytes(encode(255u), {0xF7, 0xFF}));
static_assert(same_bytes(encode(256u), {0xF8, 0x01, 0x00}));
static_assert(same_bytes(encode(0xFFFFFFFFu), {0xFA, 0xFF, 0xFF, 0xFF, 0xFF}));

// varint: zigzag (0,-1,1,-2,2 -> 0,1,2,3,4), then varuint
static_assert(same_bytes(encode(0), {0x00}));
static_assert(same_bytes(encode(-1), {0x01}));
static_assert(same_bytes(encode(1), {0x02}));
static_assert(same_bytes(encode(-2), {0x03}));
static_assert(same_bytes(encode(123), {0xF6}));
static_assert(same_bytes(encode(-123), {0xF5}));

// composites: <count> prefix for collections, <index> for variants,
// <has_value> for optional, no prefix at all for a fixed size tuple
static_assert(same_bytes(encode(std::vector<int>{1, 2, 3}), {0x03, 0x02, 0x04, 0x06}));
static_assert(same_bytes(encode(std::optional<int>{}), {0x00}));
static_assert(same_bytes(encode(std::optional<int>{5}), {0x01, 0x0A}));
static_assert(same_bytes(encode(TestVariant{7}), {0x00, 0x0E}));
static_assert(same_bytes(encode(std::tuple<int, bool>{5, true}), {0x0A, 0x01}));
static_assert(same_bytes(encode(std::string("abc")), {0x03, 0xC2, 0xC4, 0xC6}));

//double is a fixed size little endian blob, not a varint
static_assert(encoded_size(1.0) == sizeof(double));
static_assert(encoded_size(0.0f) == sizeof(float));

// ---------------------------------------------------------------------------
// 5. layout and schema
// ---------------------------------------------------------------------------

static_assert(srl::layout_of_type<Inner>.type == srl::LayoutType::sequence);
static_assert(srl::layout_of_type<Inner>.fields.size() == 2);
static_assert(srl::layout_of_type<Inner>.fields[0].field_name == "foo");
static_assert(srl::layout_of_type<Inner>.fields[1].field_name == "bar");
//regression: init_layout used to record 'const int &' while the schema keys on 'int'
static_assert(srl::layout_of_type<Inner>.fields[0].type_name == srl::type_name<int>);
static_assert(srl::layout_of_type<Inner>.fields[1].type_name == srl::type_name<double>);

static_assert(srl::layout_of_type<double>.type == srl::LayoutType::trivial);
static_assert(srl::layout_of_type<double>.blob_size == sizeof(double));
static_assert(srl::layout_of_type<unsigned>.type == srl::LayoutType::varuint);
static_assert(srl::layout_of_type<int>.type == srl::LayoutType::varint);
static_assert(srl::layout_of_type<std::vector<int> >.type == srl::LayoutType::collection);
static_assert(srl::layout_of_type<TestMap>.type == srl::LayoutType::dictionary);
static_assert(srl::layout_of_type<std::optional<int> >.type == srl::LayoutType::optional);
static_assert(srl::layout_of_type<TestVariant>.type == srl::LayoutType::variant);
static_assert(srl::layout_of_type<std::string>.type == srl::LayoutType::string);
//regression: a tuple used to describe itself as a variant
static_assert(srl::layout_of_type<std::tuple<int, bool> >.type == srl::LayoutType::sequence);

///every type_name mentioned by a layout must have its own entry in the schema,
///otherwise the schema cannot be interpreted by a reader
constexpr bool schema_is_closed(const srl::Schema &s) {
    for (const auto &[name, layout]: s.schema) {
        for (const auto &f: layout->fields) {
            bool found = false;
            for (const auto &[other, _]: s.schema) found = found || other == f.type_name;
            if (!found) return false;
        }
    }
    return true;
}

constexpr bool schema_contains(const srl::Schema &s, std::string_view name) {
    for (const auto &[k, v]: s.schema) if (k == name) return true;
    return false;
}

static_assert(schema_is_closed(srl::Schema::create<Outer>()));
static_assert(schema_is_closed(srl::Schema::create<TestVariant>()));
static_assert(schema_is_closed(srl::Schema::create<std::vector<Inner> >()));

//regression: iterate_fields reported the owning type instead of the field type,
//so recursive_walk stopped immediately and nested types never reached the schema
static_assert(srl::Schema::create<Outer>().root_type == srl::type_name<Outer>);
static_assert(schema_contains(srl::Schema::create<Outer>(), srl::type_name<Inner>));
static_assert(schema_contains(srl::Schema::create<Outer>(), srl::type_name<double>));

///the same layout must always hash the same, a different one must not
struct InnerRenamed {
    int foo = {};
    double bar = {};
    constexpr void serialize(this auto &self, auto &ar) {
        ar(self.foo, "foo");
        ar(self.bar, "BAR");       //only the field name differs from Inner
    }
};
struct InnerRetyped {
    int foo = {};
    float bar = {};
    constexpr void serialize(this auto &self, auto &ar) {
        ar(self.foo, "foo");
        ar(self.bar, "bar");       //only the field type differs from Inner
    }
};

static_assert(srl::schema_hash<Inner> == srl::schema_hash<Inner>);
static_assert(srl::schema_hash<Inner> != srl::schema_hash<InnerRenamed>);
static_assert(srl::schema_hash<Inner> != srl::schema_hash<InnerRetyped>);
static_assert(srl::schema_hash<Inner> != srl::schema_hash<Outer>);

// ---------------------------------------------------------------------------
// runtime part: containers that are not literal types, plus the throwing paths
// ---------------------------------------------------------------------------

static void test_non_literal_containers() {
    std::map<std::string, int> m{{"aaa", 1}, {"bbb", 2}, {"ccc", 3}};
    CHECK(roundtrip(m));
    CHECK(roundtrip(std::map<std::string, int>{}));
    //<count> aaa->1 bbb->2 ccc->3 ; each key is <len> + 3 chars, each value a varint
    CHECK_EQUAL(encoded_size(m), std::size_t(1 + 3 * (1 + 3 + 1)));

    std::set<int> s{3, 1, 2};
    CHECK(roundtrip(s));
    CHECK_EQUAL(encoded_size(s), std::size_t(4));

    //regression: set deserialization never read the elements, it only inserted defaults
    CHECK_EQUAL(decode<std::set<int> >(encode(s)).size(), std::size_t(3));
    CHECK(decode<std::set<int> >(encode(s)).contains(3));

    std::map<std::string, std::vector<Inner> > nested{{"k", {{1, 1.5}, {2, 2.5}}}};
    CHECK(roundtrip(nested));
}

static void test_truncated_input() {
    //a stream that ends in the middle of a value must be reported, not read past
    Bytes full = encode(Outer{{1, 0.25}, "name", {1, 2, 3}});
    CHECK_GREATER(full.size(), std::size_t(4));
    for (std::size_t cut = 0; cut < full.size(); ++cut) {
        Bytes partial(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut));
        Outer out;
        CHECK_EXCEPTION(std::runtime_error,
            srl::deserialize_from(partial.begin(), partial.end(), out));
    }
}

static void test_corrupted_input() {
    //variant index past the last alternative
    Bytes bad_index{0x05, 0x00};
    TestVariant v;
    CHECK_EXCEPTION(std::runtime_error,
        srl::deserialize_from(bad_index.begin(), bad_index.end(), v));

    //varuint prefix claiming more trailing bytes than the target type can hold.
    //this used to write past the end of a stack buffer
    Bytes bad_width{0xFF, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::uint32_t n = 0;
    CHECK_EXCEPTION(std::runtime_error,
        srl::deserialize_from(bad_width.begin(), bad_width.end(), n));

    //the widest legal encoding for the same type must still be accepted
    Bytes widest{0xFA, 0xFF, 0xFF, 0xFF, 0xFF};
    srl::deserialize_from(widest.begin(), widest.end(), n);
    CHECK_EQUAL(n, 0xFFFFFFFFu);
}

static void test_schema_to_json() {
    Json j = srl::serialize_schema_to_json(srl::Schema::create<Inner>());
    CHECK_EQUAL(j["root"].as<std::string_view>(), srl::type_name<Inner>);
    Json inner = j["types"][srl::type_name<Inner>];
    CHECK_EQUAL(inner["layout"].as<std::string_view>(), "sequence");
    CHECK_EQUAL(inner["fields"][0].as<std::string_view>(), srl::type_name<int>);
    CHECK_EQUAL(inner["fields"][1].as<std::string_view>(), srl::type_name<double>);
    CHECK_EQUAL(inner["names"][0].as<std::string_view>(), "foo");
    CHECK_EQUAL(inner["names"][1].as<std::string_view>(), "bar");
}

int main() {
    test_non_literal_containers();
    test_truncated_input();
    test_corrupted_input();
    test_schema_to_json();
    return 0;
}
