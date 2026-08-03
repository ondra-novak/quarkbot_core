#include "check.h"

#include "quarkbot/decimal.hpp"
#include "quarkbot/serializer/serialize.hpp"
#include "quarkbot/serializer/serialize_schema_to_json.hpp"
#include "quarkbot/serializer/deserialize_from_schema.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
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
static_assert(std::is_same_v<rule_of<double>,         srl::SerializeRuleFloat<double> >);
static_assert(std::is_same_v<rule_of<float>,          srl::SerializeRuleFloat<float> >);
static_assert(std::is_same_v<rule_of<Inner>,          srl::SerializeRuleWithMethod<Inner> >);

//narrow integers cannot profit from a varint, so they get a fixed width field of
//their own rather than falling into the opaque trivial blob
static_assert(std::is_same_v<rule_of<char>,           srl::SerializeRuleFixedInt<char> >);
static_assert(std::is_same_v<rule_of<std::uint8_t>,   srl::SerializeRuleFixedInt<std::uint8_t> >);
static_assert(std::is_same_v<rule_of<std::int16_t>,   srl::SerializeRuleFixedInt<std::int16_t> >);
static_assert(std::is_same_v<rule_of<char16_t>,       srl::SerializeRuleFixedInt<char16_t> >);
static_assert(std::is_same_v<rule_of<bool>,           srl::SerializeRuleBool>);
//char32_t is wide enough for a varint to pay off, so it stays compressed
static_assert(std::is_same_v<rule_of<char32_t>,       srl::SerializeRuleUnsignedNumber<char32_t> >);
//long double has no portable representation - it is the one number left opaque
static_assert(std::is_same_v<rule_of<long double>,    srl::SerializeRuleTrivial<long double> >);

enum class Side: int {buy, sell};
enum class Flags: std::uint8_t {none = 0, all = 200};
static_assert(std::is_same_v<rule_of<Side>,  srl::SerializeRuleEnum<Side> >);
static_assert(std::is_same_v<rule_of<Flags>, srl::SerializeRuleEnum<Flags> >);
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

//fixed width integers, at both ends of their range
static_assert(roundtrip<std::uint8_t>(0));
static_assert(roundtrip<std::int8_t>(-128));
static_assert(roundtrip<std::int8_t>(127));
static_assert(roundtrip<char>('A'));
static_assert(roundtrip<std::uint16_t>(0));
static_assert(roundtrip<std::uint16_t>(65535));
static_assert(roundtrip<std::int16_t>(-32768));
static_assert(roundtrip<std::int16_t>(32767));
static_assert(roundtrip<char16_t>(u'€'));
static_assert(roundtrip(Side::sell));
static_assert(roundtrip(Flags::all));

static_assert(roundtrip(std::string{}));
static_assert(roundtrip(std::string("hello world")));
static_assert(roundtrip(std::vector<int>{}));
static_assert(roundtrip(std::vector<int>{1, -2, 3000}));
static_assert(roundtrip(std::optional<int>{}));
static_assert(roundtrip(std::optional<int>{42}));
static_assert(roundtrip(std::optional<std::string>{"x"}));
static_assert(roundtrip(std::tuple<int, bool, double>{7, true, 1.5}));
static_assert(roundtrip(TestVariant{123}));
#if !defined(__clang__) || __clang_major__ > 18
static_assert(roundtrip(TestVariant{std::string("abc")}));
#endif

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
static_assert(same_bytes(encode(247u), {0xF7, 0x1}));
static_assert(same_bytes(encode(255u), {0xF7, 0x9}));
static_assert(same_bytes(encode(256u), {0xF7, 0xa}));
static_assert(same_bytes(encode(501u), {0xF7, 0xFF}));
static_assert(same_bytes(encode(502u), {0xF8, 0x01, 0x00}));
static_assert(same_bytes(encode(0xFFFFFFFFu), {0xFA, 0xFF, 0xFF, 0xFF, 0x09}));

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
static_assert(same_bytes(encode(std::string("abc")), {0x03, 97, 98, 99}));

//fixed width integers: <byte_size> bytes, little endian, signed values as two's
//complement. The byte order is part of the format, not whatever the host does -
//going through the trivial rule made these host-endian and unportable
static_assert(same_bytes(encode(std::uint16_t(0x0102)), {0x02, 0x01}));
static_assert(same_bytes(encode(std::int16_t(-2)), {0xFE, 0xFF}));
static_assert(same_bytes(encode(std::int16_t(-32768)), {0x00, 0x80}));
static_assert(same_bytes(encode(std::uint8_t(255)), {0xFF}));
static_assert(same_bytes(encode(std::int8_t(-1)), {0xFF}));
static_assert(same_bytes(encode('A'), {0x41}));
//...unlike a varint, a narrow integer never grows above its own width
static_assert(encoded_size(std::uint8_t(255)) == 1);
static_assert(encoded_size(std::uint16_t(65535)) == 2);

//bool is one normalized byte
static_assert(same_bytes(encode(true), {0x01}));
static_assert(same_bytes(encode(false), {0x00}));

//IEEE-754, little endian
static_assert(same_bytes(encode(1.0), {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}));
static_assert(same_bytes(encode(-0.5f), {0x00, 0x00, 0x00, 0xBF}));
static_assert(encoded_size(1.0) == sizeof(double));
static_assert(encoded_size(0.0f) == sizeof(float));

//an enum is stored exactly as its underlying type, so an enum over int is a
//varint (one byte here) and one over uint8_t a single fixed byte
static_assert(same_bytes(encode(Side::sell), {0x02}));
static_assert(same_bytes(encode(Flags::all), {0xC8}));

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

static_assert(srl::layout_of_type<double>.type == srl::LayoutType::floating);
static_assert(srl::layout_of_type<double>.byte_size == sizeof(double));
static_assert(srl::layout_of_type<float>.type == srl::LayoutType::floating);
static_assert(srl::layout_of_type<float>.byte_size == sizeof(float));
static_assert(srl::layout_of_type<unsigned>.type == srl::LayoutType::varuint);
static_assert(srl::layout_of_type<int>.type == srl::LayoutType::varint);

//a narrow integer must describe itself as a number of a known width and sign.
//regression: these all used to report {trivial, <sizeof>}, which left a reader of
//the schema unable to tell an int16 from a uint16 or from any other 2 byte POD
static_assert(srl::layout_of_type<std::uint16_t>.type == srl::LayoutType::fixed_uint);
static_assert(srl::layout_of_type<std::uint16_t>.byte_size == 2);
static_assert(srl::layout_of_type<std::int16_t>.type == srl::LayoutType::fixed_sint);
static_assert(srl::layout_of_type<std::int16_t>.byte_size == 2);
static_assert(srl::layout_of_type<std::uint8_t>.byte_size == 1);
static_assert(srl::layout_of_type<bool>.type == srl::LayoutType::boolean);
static_assert(srl::layout_of_type<bool>.byte_size == 1);
static_assert(srl::layout_of_type<std::uint16_t>.get_hash() != srl::layout_of_type<std::int16_t>.get_hash());

//byte_size is nonzero exactly for the fixed width leaf layouts
static_assert(srl::layout_of_type<unsigned>.byte_size == 0);
static_assert(srl::layout_of_type<std::string>.byte_size == 0);
static_assert(srl::layout_of_type<Inner>.byte_size == 0);
static_assert(srl::layout_of_type<Side>.byte_size == 0);

//an enum wraps its underlying type as its single field, the way optional wraps
//its value type - the width and the signedness come from that type's own layout
static_assert(srl::layout_of_type<Side>.type == srl::LayoutType::enumeration);
static_assert(srl::layout_of_type<Side>.fields.size() == 1);
static_assert(srl::layout_of_type<Side>.fields[0].type_name == srl::type_name<int>);
static_assert(srl::layout_of_type<Flags>.fields[0].type_name == srl::type_name<std::uint8_t>);
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
//the underlying type an enum points at must be reachable from the schema too
static_assert(schema_is_closed(srl::Schema::create<Side>()));
static_assert(schema_contains(srl::Schema::create<Side>(), srl::type_name<int>));

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
    //(the trailing bytes carry the value biased down by single_byte_max)
    Bytes widest{0xFA, 0xFF, 0xFF, 0xFF, 0x09};
    srl::deserialize_from(widest.begin(), widest.end(), n);
    CHECK_EQUAL(n, 0xFFFFFFFFu);
}

static void test_bool_normalization() {
    //a bool holding anything but 0 or 1 has no valid representation and poisons
    //every later use of it. Reading it as a raw blob used to do exactly that:
    //the byte 0x02 survived into the bool and `b == true` evaluated to 2
    for (std::uint8_t raw: {std::uint8_t(2), std::uint8_t(0x80), std::uint8_t(0xFF)}) {
        bool b = decode<bool>(Bytes{raw});
        CHECK(b);
        CHECK_EQUAL(b, true);
        std::uint8_t stored = 0;
        std::memcpy(&stored, &b, 1);
        CHECK_EQUAL(stored, std::uint8_t(1));
    }
    CHECK_EQUAL(decode<bool>(Bytes{0x00}), false);
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
    //a fixed width leaf must publish its width, everything else must not claim one
    CHECK_EQUAL(j["types"][srl::type_name<double>]["layout"].as<std::string_view>(), "floating");
    CHECK_EQUAL(j["types"][srl::type_name<double>]["byte_size"].as<std::size_t>(), std::size_t(8));
    CHECK(inner["byte_size"].is_null());

    Json e = srl::serialize_schema_to_json(srl::Schema::create<Side>());
    CHECK_EQUAL(e["types"][srl::type_name<Side>]["layout"].as<std::string_view>(), "enumeration");
    CHECK_EQUAL(e["types"][srl::type_name<Side>]["fields"][0].as<std::string_view>(), srl::type_name<int>);
    CHECK_EQUAL(e["types"][srl::type_name<int>]["layout"].as<std::string_view>(), "varint");

    Json s = srl::serialize_schema_to_json(srl::Schema::create<std::uint16_t>());
    CHECK_EQUAL(s["types"][srl::type_name<std::uint16_t>]["layout"].as<std::string_view>(), "fixed_uint");
    CHECK_EQUAL(s["types"][srl::type_name<std::uint16_t>]["byte_size"].as<std::size_t>(), std::size_t(2));
}

struct OuterOuterDecimal {
    Decimal dec_val = {};
    Outer outer;
    constexpr void serialize(this auto &self, auto &arch) {
        arch(self.dec_val, "dec_val");
        arch(self.outer, "outer");
    }
};

void test_deserialize_from_schema() {
    OuterOuterDecimal subject = {
        3.1415_dec,
        {
            {
                42,0.5
            }, "test_name",{1,2,3,4}
        }
    };
    std::string serialized;
    srl::serialize_to<char>(subject, std::back_inserter(serialized));
    Json schema = srl::serialize_schema_to_json(srl::Schema::create(subject));

    auto dsrl = srl::string_deserializer(serialized);
    Json output = srl::deserialize_from_schema(schema,dsrl , [](std::string_view type, std::string_view content) {
        if (type == "Decimal") {
            Decimal val;
            srl::deserialize_from(content.begin(), content.end(), val);
            return val.to_string();
        } else {
            return srl::default_resolver(type, content);
        }
    });
    CHECK_EQUAL(output.to_string() , R"json({"dec_val":"3.1415","outer":{"inner":{"foo":42,"bar":0.5},"name":"test_name","items":[1,2,3,4]}})json");

}

// ---------------------------------------------------------------------------
// schema driven deserialization - every layout has to be decodable from the
// JSON schema alone, without the originating C++ type
// ---------------------------------------------------------------------------

///encode a value, then decode the bytes using nothing but the JSON schema, the
///way an external reader (the javascript on the web side) has to
template<typename T>
static std::string via_schema(const T &val, auto &&resolver) {
    std::string bin;
    srl::serialize_to<char>(val, std::back_inserter(bin));
    Json schema = srl::serialize_schema_to_json(srl::Schema::create<T>());
    auto rd = srl::string_deserializer(bin);
    return srl::deserialize_from_schema(schema, rd, resolver).to_string();
}

template<typename T>
static std::string via_schema(const T &val) {
    return via_schema(val, srl::default_resolver);
}

///a field emitted without a name next to one that has it - the reader has to
///invent something for the unnamed slot
struct MixedNames {
    int a = {};
    int b = {};
    constexpr void serialize(this auto &self, auto &ar) {
        ar(self.a);
        ar(self.b, "b");
    }
};

enum class Wide: std::uint16_t {small = 5, big = 300};

static void test_schema_leaf_layouts() {
    CHECK_EQUAL(via_schema(true), "true");
    CHECK_EQUAL(via_schema(false), "false");

    //fixed width integers, at both ends of every supported width
    CHECK_EQUAL(via_schema(std::uint8_t(255)), "255");
    CHECK_EQUAL(via_schema(std::int8_t(-128)), "-128");
    CHECK_EQUAL(via_schema(std::uint16_t(65535)), "65535");
    CHECK_EQUAL(via_schema(std::int16_t(-32768)), "-32768");
    //a bare char reads back as its numeric value - only a string turns into text
    CHECK_EQUAL(via_schema('A'), "65");

    //varints, at the range limits where the width prefix is widest
    CHECK_EQUAL(via_schema(std::uint32_t(0xFFFFFFFF)), "4294967295");
    CHECK_EQUAL(via_schema(std::uint64_t(0xFFFFFFFFFFFFFFFFULL)), "18446744073709551615");
    CHECK_EQUAL(via_schema(std::numeric_limits<std::int64_t>::min()), "-9223372036854775808");
    CHECK_EQUAL(via_schema(0), "0");

    CHECK_EQUAL(via_schema(1.5f), "1.5");
    CHECK_EQUAL(via_schema(0.5), "0.5");
    //presentation limit, not a decode limit: JsonNumber formats with {:.12g}
    CHECK_EQUAL(via_schema(3.141592653589793), "3.14159265359");

    //long double has no portable representation, so it stays an opaque blob and
    //the resolver is the only thing that can say anything about it
    CHECK_EQUAL(via_schema(1.0L), R"("Binary size: 16")");
}

static void test_schema_composite_layouts() {
    CHECK_EQUAL(via_schema(std::vector<int>{1, 2, 3}), "[1,2,3]");
    CHECK_EQUAL(via_schema(std::vector<int>{}), "[]");
    CHECK_EQUAL(via_schema(std::set<int>{2, 1}), "[1,2]");

    //a dictionary decodes as a list of [key,value] pairs - a JSON object cannot
    //be used because the keys are not necessarily strings
    CHECK_EQUAL(via_schema(std::map<std::string, int>{{"a", 1}, {"b", 2}}),
                R"([["a",1],["b",2]])");
    CHECK_EQUAL(via_schema(std::map<int, bool>{{7, true}}), "[[7,true]]");

    CHECK_EQUAL(via_schema(std::optional<int>{}), "null");
    CHECK_EQUAL(via_schema(std::optional<int>{7}), "7");
    CHECK_EQUAL(via_schema(std::optional<std::string>{"x"}), R"("x")");

    //a variant decodes as the alternative that was actually stored
    CHECK_EQUAL(via_schema(TestVariant{7}), "7");
    CHECK_EQUAL(via_schema(TestVariant{std::string("x")}), R"("x")");

    //named fields make an object, an unnamed sequence (a tuple) makes an array
    CHECK_EQUAL(via_schema(Inner{42, 0.5}), R"({"foo":42,"bar":0.5})");
    CHECK_EQUAL(via_schema(std::tuple<int, bool>{5, true}), "[5,true]");
    CHECK_EQUAL(via_schema(MixedNames{1, 2}), R"({"#1":1,"b":2})");

    //a string of char is text; any wider character type falls back to a list of
    //code units, because the schema cannot promise a JSON encodable encoding
    CHECK_EQUAL(via_schema(std::string("ahoj")), R"("ahoj")");
    CHECK_EQUAL(via_schema(std::string()), R"("")");
    CHECK_EQUAL(via_schema(std::u16string(u"ab")), "[97,98]");
}

static void test_schema_enums() {
    //an enum is stored as its underlying type, so the reader has to decode it
    //through that type - not by assuming int
    CHECK_EQUAL(via_schema(Side::sell), "1");
    CHECK_EQUAL(via_schema(Flags::all), "200");
    CHECK_EQUAL(via_schema(Wide::big), "300");

    //...and a desync shows up as soon as anything follows the enum
    CHECK_EQUAL(via_schema(std::tuple<Wide, std::string>{Wide::big, "after"}),
                R"([300,"after"])");
    CHECK_EQUAL(via_schema(std::tuple<Flags, int>{Flags::all, -5}), "[200,-5]");
}

static void test_schema_resolver() {
    //the resolver gets the type name and exactly the bytes the schema claims
    std::vector<std::pair<std::string, std::size_t> > seen;
    auto spy = [&](std::string_view type, std::string_view content) -> Json {
        seen.emplace_back(std::string(type), content.size());
        return std::string("blob");
    };
    CHECK_EQUAL(via_schema(std::tuple<long double, int>{1.0L, 3}, spy), R"(["blob",3])");
    CHECK_EQUAL(seen.size(), std::size_t(1));
    CHECK_EQUAL(seen[0].first, srl::type_name<long double>);
    CHECK_EQUAL(seen[0].second, sizeof(long double));

    //Decimal is the real motivation: a trivially copyable type whose bytes only
    //the owning application can interpret
    auto dec_resolver = [](std::string_view type, std::string_view content) -> Json {
        if (type == srl::type_name<Decimal>) {
            Decimal val;
            srl::deserialize_from(content.begin(), content.end(), val);
            return val.to_string();
        }
        return srl::default_resolver(type, content);
    };
    CHECK_EQUAL(via_schema(std::vector<Decimal>{1.5_dec, -2.25_dec}, dec_resolver),
                R"(["1.5","-2.25"])");
}

///replaces one key of one type description, leaving the rest of the schema alone
static Json patch_type(const Json &schema, std::string_view type,
                       std::string_view key, Json value) {
    Json::Object types;
    for (const auto &[k, v]: schema["types"].as_object()) {
        Json::Object t;
        for (const auto &[tk, tv]: v.as_object()) t[tk] = tv;
        if (k == type) t[key] = value;
        types[k] = Json(std::move(t));
    }
    return Json{{"root", schema["root"].as_text()}, {"types", Json(std::move(types))}};
}

static void test_schema_broken_schema() {
    //a schema that does not describe the data must be reported, never guessed at
    std::string bin;
    srl::serialize_to<char>(Inner{1, 2.0}, std::back_inserter(bin));

    auto run = [&](const Json &schema) {
        auto rd = srl::string_deserializer(bin);
        return srl::deserialize_from_schema(schema, rd, srl::default_resolver);
    };

    Json good = srl::serialize_schema_to_json(srl::Schema::create<Inner>());
    //the baseline has to work, otherwise the rest of this proves nothing
    CHECK_EQUAL(run(good).to_string(), R"({"foo":1,"bar":2})");

    CHECK_EXCEPTION(std::runtime_error,
        run(patch_type(good, srl::type_name<Inner>, "layout", "nonsense")));

    CHECK_EXCEPTION(std::runtime_error,
        run(patch_type(good, srl::type_name<Inner>, "fields",
                       Json(Json::Array{Json("NoSuchType")}))));

    Json missing_root = Json{{"root", "NoSuchType"}, {"types", good["types"]}};
    CHECK_EXCEPTION(std::runtime_error, run(missing_root));

    //a fixed width leaf without a width cannot be read at all - consuming zero
    //bytes and carrying on would silently desync everything after it
    CHECK_EXCEPTION(std::runtime_error,
        run(patch_type(good, srl::type_name<double>, "byte_size", Json())));
    CHECK_EXCEPTION(std::runtime_error,
        run(patch_type(good, srl::type_name<double>, "byte_size", 0)));

    //a variant index past the last alternative, the same corruption the typed
    //deserializer already rejects
    std::string var_bin;
    srl::serialize_to<char>(TestVariant{7}, std::back_inserter(var_bin));
    var_bin[0] = 0x05;
    Json var_schema = srl::serialize_schema_to_json(srl::Schema::create<TestVariant>());
    auto var_rd = srl::string_deserializer(var_bin);
    CHECK_EXCEPTION(std::runtime_error,
        srl::deserialize_from_schema(var_schema, var_rd, srl::default_resolver));

    //data that ends early must throw rather than return a half decoded value
    for (std::size_t cut = 0; cut < bin.size(); ++cut) {
        auto rd = srl::string_deserializer(std::string_view(bin).substr(0, cut));
        CHECK_EXCEPTION(std::runtime_error,
            srl::deserialize_from_schema(good, rd, srl::default_resolver));
    }
}

int main() {
    test_non_literal_containers();
    test_truncated_input();
    test_corrupted_input();
    test_bool_normalization();
    test_schema_to_json();
    test_deserialize_from_schema();
    test_schema_leaf_layouts();
    test_schema_composite_layouts();
    test_schema_enums();
    test_schema_resolver();
    test_schema_broken_schema();
    return 0;
}
