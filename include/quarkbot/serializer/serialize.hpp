
#pragma once

#include "../utils/type_name.hpp"
#include "quarkbot/hash/fnv1a.hpp"
#include "quarkbot/serializer/schema_fwd.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

template<typename T> concept BinaryReader = std::is_invocable_v<T, std::span<std::uint8_t> >;
template<typename T> concept BinaryWriter = std::is_invocable_v<T, std::span<const std::uint8_t> >;    

namespace srl { 

template<typename T>
concept IsOptional = requires(T v) {
    typename T::value_type;
    v.value();
    v.emplace();
    v.has_value();    // metoda has_value
};

template<typename T>
concept IsLinearContainer = std::ranges::range<T> && requires(T v, std::size_t sz) {
    typename T::value_type;
    { v.resize(sz) };
};

template<typename T>
concept IsMap = std::ranges::range<T> && requires(T v, typename T::key_type k) {
    typename T::key_type;
    typename T::mapped_type;
    { v.clear() };
    { v[k]} -> std::same_as<typename T::mapped_type &>;
};

template<typename T>
concept IsSet = std::ranges::range<T> && !IsMap<T> &&  requires(T v, typename T::value_type nv) {
    typename T::value_type;
    { v.clear() };
    { v.emplace(nv) };
};


template<typename T>
concept IsTupleLike = !std::ranges::range<T> && requires {
    typename std::tuple_size<T>::type;
};

template<typename T>
concept IsVariantLike = !std::ranges::range<T> && requires {
    typename std::variant_size<T>::type;
};

template<typename T>
concept IsIntegralNumber = std::is_integral_v<T> && std::is_arithmetic_v<T>;



template<typename T> concept HasSerializeMethod = requires(const T &rd, T &wr, decltype([](auto){}) &archive) {
    {rd.serialize(archive)};
    {wr.serialize(archive)};
};

struct TestString {
    template<typename T>
    static std::integral_constant<bool, true> test(const std::basic_string<T> &v);
    static std::integral_constant<bool, false> test(...);
};

struct TestStringView {
    template<typename T>
    static std::integral_constant<bool, true> test(const std::basic_string_view<T> &v);
    static std::integral_constant<bool, false> test(...);
};

template<typename T>
concept IsString = decltype(TestString::test(std::declval<T>()))::value;
template<typename T>
concept IsStringView = decltype(TestStringView::test(std::declval<T>()))::value;


enum class LayoutType {
    sequence,       //sequence of fields
    collection,     //collection of items (not dictionary)
    dictionary,     //associative collection - key=value
    variant,        //list of variants
    optional,       //optional field
    trivial,        //binary blob of fixed size
    string,         //<size><characters>
    varuint,        //compresed unsigned  (prefixed, 0-246 inline number, n-247 - count of following bytes - big endian)
    varint,         //compresed signed (zigzag)
};

struct FieldDef {
    std::string_view type_name;
    std::string_view field_name;
};



struct LayoutBase {
    LayoutType type = {};
    std::size_t blob_size = {};
    std::span<FieldDef> fields = {}; 

    constexpr LayoutBase() = default;
    constexpr LayoutBase(const LayoutBase &) = delete;
    constexpr LayoutBase &operator=(const LayoutBase &) = delete;
    constexpr std::size_t get_hash() const {
        std::size_t ret = 0;
        for (auto &f: fields) {
            ret = hash_combine(hash_combine(ret,
                fnv1a_hash(f.field_name)),
                fnv1a_hash(f.type_name));
        }
        ret = hash_combine(ret, blob_size);
        ret = hash_combine(ret, static_cast<std::size_t>(type));
        return ret;
    }
};


template<typename T>
concept SerializeRule = requires(LayoutBase &layout_base,                                
                decltype([](auto){}) type_iterator,
                decltype([](auto){}) data_iterator,
                const typename T::value_type  &input, typename T::value_type &output) {
    //returns required count of fields
    {T::field_count()} -> std::convertible_to<std::size_t>;
    //iterate all fields, feeds iterator with type identity object
    {T::iterate_fields(type_iterator)};    
    //initialize layout structure
    {T::init_layout(layout_base)};
    //serialize content
    {T::serialize(input, data_iterator)};
    //deserialize content
    {T::deserialize(output, data_iterator)};
};


template<typename T>
struct UninitialzedObject {
    union DummyUnion {
        T val;
        constexpr DummyUnion() {} 
        constexpr ~DummyUnion() {}
    };
    inline static DummyUnion obj{};
};

template<typename T>
concept IsStringLiteral = requires {    
    []<std::size_t N>(const char (&)[N]){}(std::declval<T>());
};


template<HasSerializeMethod T>
struct SerializeRuleWithMethod {
    using value_type = T;
    static constexpr std::size_t field_count() {        
        std::size_t counter = 0;
        const auto &obj = UninitialzedObject<T>::obj;
        auto countfn = [&](auto && ... ){++counter;};
        obj.val.serialize(countfn);
        return counter;
    }
    static constexpr void iterate_fields(auto &&cb) {
        const auto &obj = UninitialzedObject<T>::obj;
        auto iter = [&]<typename X, typename ... Args>(const X &, Args &&... ){
            cb(std::type_identity<std::decay_t<X> >());
        };
        obj.val.serialize(iter);
    }
    static constexpr void init_layout(LayoutBase &l) {
        l.type =  LayoutType::sequence;
        auto beg = l.fields.begin();       
        const auto &obj = UninitialzedObject<T>::obj; 
        auto iter = [&]<typename X, typename ... Args>(X &&, Args &&...name){
            //decay: X binds as 'const F &' here, but iterate_fields/Schema key on F
            beg->type_name = type_name<std::decay_t<X> >;
            static_assert(sizeof...(Args)<2, "Serialization operator can have 1 or 2 arguments, not more");
            if constexpr(sizeof...(Args) == 1) {
                static_assert((IsStringLiteral<Args> && ...), "Second argument must be string literal");
                beg->field_name = (name,...);
            } else {
                beg->field_name = {};
            }
            ++beg;
        };
        obj.val.serialize(iter);
    }
    static constexpr void serialize(const T &s, auto &&cb) {
        s.serialize(cb);
    }
    static constexpr void deserialize(T &s, auto &&cb) {
        s.serialize(cb);
    }
};

template<IsTupleLike T>
struct SerializeRuleTupleLike {
    using value_type = T;
    static constexpr auto fld_count =std::tuple_size_v<T>;

    static constexpr std::size_t field_count() {        
        return fld_count;
    }
    

    static constexpr void iterate_fields(auto &&cb) {
        auto iter = [&]<std::size_t ... idx>(std::index_sequence<idx...>) {
            (cb(std::type_identity<std::decay_t<decltype(std::get<idx>(std::declval<T>()))> >{}),...);
        };
        iter(std::make_index_sequence<fld_count>{});

    }
    static constexpr void init_layout(LayoutBase &l) {
        l.type =  LayoutType::sequence;
        auto beg = l.fields.begin();
        iterate_fields([&]<typename X>(std::type_identity<X>){
            beg->type_name = type_name<X>;
            beg->field_name = {};
            ++beg;
        });
    }
    static constexpr void serialize(const T &s, auto &&cb) {
        auto iter = [&]<std::size_t ... idx>(std::index_sequence<idx...>) {
            (cb(std::get<idx>(s)),...);
        };
        iter(std::make_index_sequence<fld_count>{});

    }
    static constexpr void deserialize(T &s, auto &&cb) {
        auto iter = [&]<std::size_t ... idx>(std::index_sequence<idx...>) {
            (cb(std::get<idx>(s)),...);
        };
        iter(std::make_index_sequence<fld_count>{});
    }
};

template<IsVariantLike T>
struct SerializeRuleVariantLike {
    using value_type = T;
    static constexpr auto fld_count =std::variant_size_v<T>;

    static constexpr std::size_t field_count() {        
        return fld_count;
    }
    
    static constexpr void iterate_fields(auto &&cb) {
        auto iter = [&]<std::size_t ... idx>(std::index_sequence<idx...>) {
            (cb(std::type_identity<std::decay_t<decltype(std::get<idx>(std::declval<T>()))> >{}),...);
        };
        iter(std::make_index_sequence<fld_count>{});

    }
    static constexpr void init_layout(LayoutBase &l) {
        l.type =  LayoutType::variant;
        auto beg = l.fields.begin();
        iterate_fields([&]<typename X>(std::type_identity<X>){
            beg->type_name = type_name<X>;
            beg->field_name = {};
            ++beg;
        });
    }
    static constexpr void serialize(const T &s, auto &&cb) {
        std::size_t idx = s.index();
        cb(idx);
        std::visit([&](const auto &x){
            cb(x);
        }, s);        
    }

    static constexpr auto create_init_table() {        
        auto iter = [&]<std::size_t ... is>(std::index_sequence<is...>) {
            return std::array<void (*)(T &), fld_count>{[](T &s){s.template emplace<is>();}...};
        };
        return iter(std::make_index_sequence<fld_count>{});
    }

    static constexpr auto variant_init_table_by_index = create_init_table();

    static constexpr void deserialize(T &s, auto &&cb) {
        std::size_t idx;
        cb(idx);
        if (idx >=  fld_count) throw std::runtime_error(std::format("Variant index for {} is out of range. index={}, maximum={}. Corrupted data", type_name<T>, idx, fld_count-1));
        variant_init_table_by_index[idx](s);
        std::visit([&](auto &x){
            cb(x);
        }, s);
    }
};

template<typename T>
struct SerializeRuleUnsignedNumber {
    using value_type = T;
    static constexpr std::size_t field_count() {return 0;}
    static constexpr void iterate_fields(auto) {}
    static constexpr void init_layout(LayoutBase &l) {
        l.type = LayoutType::varuint;        
    }
    static constexpr void serialize(const T &s, auto &&cb) {
        auto b = static_cast<std::uintmax_t>(s);
        if (b <= 246) {
            auto n = static_cast<std::uint8_t>(b);
            write_binary(cb, std::span(&n,1));
        } else {
            std::array<std::uint8_t, sizeof(T) + 1> buffer;
            auto iter = buffer.begin();
            auto t = b;
            auto c = 0;
            while (t) {
                ++c;
                t >>= 8;
            }
            *iter++ = static_cast<std::uint8_t>(c+246);
            while (c--) {
                *iter++ = static_cast<std::uint8_t>((b>>(c*8)) & 0xFF);
            }
            write_binary(cb, std::span(buffer.begin(), iter));
        }
    }
    static constexpr void deserialize(T &s, auto &&cb) {
        std::uint8_t pfx = {};
        read_binary(cb, std::span(&pfx,1));
        if (pfx <=246) {
            s = static_cast<T>(pfx);
        } else {
            //the prefix is attacker/corruption controlled - it can claim up to 9
            //trailing bytes, which must not be written past the end of buffer
            const std::size_t count = static_cast<std::size_t>(pfx) - 246;
            if (count > sizeof(T)) throw std::runtime_error(std::format(
                    "Varuint of {} declares {} trailing bytes, maximum is {}. Corrupted data",
                    type_name<T>, count, sizeof(T)));
            std::array<std::uint8_t, sizeof(T)> buffer = {};
            read_binary(cb, std::span(buffer.data(), count));
            s = 0;
            for (std::size_t i = 0; i < count; ++i) {
                s = static_cast<T>((s << 8) | buffer[i]);
            }
        }

    }
};

template<typename T>
struct SerializeRuleSignedNumber {
    using value_type = T;
   static constexpr std::size_t field_count() {return 0;}
    static constexpr void iterate_fields(auto) {}
    static constexpr void init_layout(LayoutBase &l) {
        l.type = LayoutType::varint;        
    }
    using U = std::make_unsigned_t<T>;
    ///standard zigzag: 0,-1,1,-2,2 -> 0,1,2,3,4
    /**
     * Everything happens in the unsigned domain on purpose. Negating the minimum
     * value of T is undefined behaviour, so a sign-magnitude scheme cannot encode
     * it at all - this mapping is bijective over the whole range of T.
     */
    static constexpr void serialize(const T &s, auto &&cb) {
        const U mask = s < static_cast<T>(0) ? static_cast<U>(~U(0)) : U(0);
        const U zigzag = static_cast<U>(static_cast<U>(static_cast<U>(s) << 1) ^ mask);
        SerializeRuleUnsignedNumber<U>::serialize(zigzag, cb);
    }
    static constexpr void deserialize(T &s, auto &&cb) {
        U v = {};
        SerializeRuleUnsignedNumber<U>::deserialize(v, cb);
        const U mask = (v & 1) ? static_cast<U>(~U(0)) : U(0);
        s = static_cast<T>(static_cast<U>(static_cast<U>(v >> 1) ^ mask));
    }
};

template<typename T>
struct SerializeRuleString {
    using value_type = std::basic_string<T>;
   static constexpr std::size_t field_count() {return 1;}
    static constexpr void iterate_fields(auto &&cb) {
        cb(std::type_identity<T>{});
    }
    static constexpr void init_layout(LayoutBase &l) {
        l.type = LayoutType::string;        
        l.fields[0].type_name = type_name<T>;
    }
    static constexpr void serialize(const std::basic_string<T> &s, auto &&cb) {        
        std::size_t sz = s.size();
        cb(sz);
        for (auto &x: s) {
            cb(x);
        }
    }
    static constexpr void deserialize(std::basic_string<T> &s, auto &&cb) {
        std::size_t sz;
        cb(sz);
        s.resize(sz);
        for (auto &x: s) {
            cb(x);
        }

    }
};

template<IsLinearContainer T>
struct SerializeRuleLinearContainer {
    using value_type = T;
    using U = T::value_type;
    static constexpr std::size_t field_count() {return 1;}
    static constexpr void iterate_fields(auto cb) {
        cb(std::type_identity<U>());
    }
    static constexpr void init_layout(LayoutBase &l) {
        l.fields[0].type_name = type_name<U>;
        l.type = LayoutType::collection;        
    }
    static constexpr void serialize(const T &s, auto &&cb) {    
        auto count = static_cast<std::size_t>(std::distance(s.begin(), s.end()));
        cb(count);
        for (const auto &x: s) {
            cb(x);
        }
    }
    static constexpr void deserialize(T &s, auto &&cb) {    
        std::size_t count;
        cb(count);
        s.resize(count);
        for (auto &x: s) {
            cb(x);
        }        
    }
};

template<IsMap T>
struct SerializeRuleMap {    
    using value_type = T;
    static constexpr std::size_t field_count() {return 2;}
    static constexpr void iterate_fields(auto cb) {
        cb(std::type_identity<typename T::key_type>());
        cb(std::type_identity<typename T::mapped_type>());
    }
    static constexpr void init_layout(LayoutBase &l) {
        l.fields[0].type_name = type_name<typename T::key_type>;
        l.fields[1].type_name = type_name<typename T::mapped_type>;
        l.type = LayoutType::dictionary;
    }
    static constexpr void serialize(const T &s, auto &&cb) {
        auto count = static_cast<std::size_t>(std::distance(s.begin(), s.end()));
        cb(count);
        for (const auto &[k, v]: s) {
            cb(k);
            cb(v);
        }
    }
    static constexpr void deserialize(T &s, auto &&cb) {    
        std::size_t count;
        cb(count);
        s.clear();
        for (std::size_t i = 0; i < count; ++i) {
            typename T::key_type k = {};
            cb(k);
            auto &v = s[k];
            cb(v);
        }
    }
};

template<IsSet T>
struct SerializeRuleSet {
    using value_type = T;
    using U = std::decay_t<decltype(*std::declval<typename T::iterator>())>;
    static constexpr std::size_t field_count() {return 1;}
    static constexpr void iterate_fields(auto cb) {
        cb(std::type_identity<typename T::value_type>());
    }
    static constexpr void init_layout(LayoutBase &l) {
        l.fields[0].type_name = type_name<typename T::value_type>;
        l.type = LayoutType::collection;        
    }
    static constexpr void serialize(const T &s, auto &&cb) {    
        auto count = static_cast<std::size_t>(std::distance(s.begin(), s.end()));
        cb(count);
        for (const auto &x: s) {
            cb(x);
        }
    }
    static constexpr void deserialize(T &s, auto &&cb) {    
        std::size_t count;
        cb(count);
        s.clear();
        for (std::size_t i = 0; i < count; ++i) {
            typename T::value_type v = {};
            cb(v);
            s.emplace(std::move(v));
        }
    }
};
template<IsOptional T>
struct SerializeRuleOptional {
    using value_type = T;
    static constexpr std::size_t field_count() {return 1;}
    static constexpr void iterate_fields(auto &&cb) {
        cb(std::type_identity<typename T::value_type>{});
    }
    static constexpr void init_layout(LayoutBase &l) {
        l.type = LayoutType::optional;
        l.fields[0].type_name = type_name<typename T::value_type>;        
    }
    static constexpr void serialize(const T &s, auto &&cb) {
        bool b = s.has_value();
        cb(b);
        if (b) {
            cb(s.value());
        }
    }
    static constexpr void deserialize(T &s, auto &&cb) {
        bool b;
        cb(b);
        if (b) {
            s.emplace();
            cb(s.value());
        }
    }
};

template<typename T>
struct SerializeRuleTrivial {
    using value_type = T;
    static constexpr std::size_t field_count() {return 0;}
    static constexpr void iterate_fields(auto) {}
    static constexpr void init_layout(LayoutBase &l) {
        l.type = LayoutType::trivial;
        l.blob_size = sizeof(T);
    }
    static constexpr void serialize(const T &s, auto &&cb) {
        write_binary(cb, std::bit_cast<std::array<std::uint8_t, sizeof(T)> >(s));
    }
    static constexpr void deserialize(T &s, auto &&cb) {
        std::array<std::uint8_t, sizeof(T)> arr;
        read_binary(cb, arr);
        s = std::bit_cast<T>(arr);
    }
};


///Built-in rules for the types this library knows about.
/**
 * These live in a nested namespace under a different name on purpose. They are
 * reached only by *qualified* lookup from the get_rule customization point, so
 * they never take part in overload resolution against a user-provided
 * get_serialize_rule(). That is what makes a user rule always win instead of
 * becoming ambiguous with a built-in one.
 */
namespace builtin {

template<typename T>
requires(HasSerializeMethod<T>)
constexpr auto rule(std::type_identity<T>) {return  SerializeRuleWithMethod<T>{};}

template<typename T>
requires(std::is_integral_v<T> && std::is_arithmetic_v<T> && std::is_unsigned_v<T>)
constexpr auto rule(std::type_identity<T>) {return SerializeRuleUnsignedNumber<T>{};}

template<typename T>
requires(std::is_integral_v<T> && std::is_arithmetic_v<T> && !std::is_unsigned_v<T>)
constexpr auto rule(std::type_identity<T>) {return SerializeRuleSignedNumber<T>{};}

template<typename T>
requires(IsMap<T> && !HasSerializeMethod<T>)
constexpr auto rule(std::type_identity<T>) {return SerializeRuleMap<T>{};}

template<typename T>
requires(IsSet<T> && !HasSerializeMethod<T>)
constexpr auto rule(std::type_identity<T>) {return SerializeRuleSet<T>{};}

template<typename T>
requires(IsLinearContainer<T> && !HasSerializeMethod<T> && !IsString<T>)
constexpr auto rule(std::type_identity<T>) {return SerializeRuleLinearContainer<T>{};}

template<typename T>
requires(IsTupleLike<T> && !HasSerializeMethod<T>)
constexpr auto rule(std::type_identity<T>) {return SerializeRuleTupleLike<T>{};}

template<typename T>
requires(IsVariantLike<T> && !HasSerializeMethod<T>)
constexpr auto rule(std::type_identity<T>) {return SerializeRuleVariantLike<T>{};}

template<typename T>
requires(IsOptional<T> && !HasSerializeMethod<T>)
constexpr auto rule(std::type_identity<T>) {return SerializeRuleOptional<T>{};}

//note: optional is excluded because std::optional<trivial> is itself trivially
//copyable, which would otherwise hide SerializeRuleOptional behind a blob
template<typename T>
requires(std::is_trivially_copyable_v<T> && !HasSerializeMethod<T> && !IsIntegralNumber<T> && !IsOptional<T>)
constexpr auto rule(std::type_identity<T>) {return SerializeRuleTrivial<T>{};}

template<typename T>
requires(IsString<T> && !HasSerializeMethod<T>)
constexpr auto rule(std::type_identity<T>) {return SerializeRuleString<typename T::value_type>{};}

}

///Escape hatch for types whose namespace you cannot extend (std::*, global, foreign libs)
/**
 * Specialize this with a type satisfying SerializeRule. An explicit
 * specialization may be written qualified from any namespace and works across
 * module boundaries, so this covers what ADL cannot reach:
 *
 * @code
 * template<> struct srl::custom_serialize_rule<Foreign> { ... };
 * @endcode
 */
template<typename T> struct custom_serialize_rule;

///Poison pill - keeps the name visible so the unqualified call below triggers ADL,
///while never being a viable candidate itself.
void get_serialize_rule() = delete;

namespace _cpo {

struct GetRule {
    template<typename T>
    constexpr auto operator()(std::type_identity<T> ident) const {
        if constexpr (requires {get_serialize_rule(ident);}) {
            //1. rule found through ADL in the namespace of T
            return get_serialize_rule(ident);
        } else if constexpr (requires {typename custom_serialize_rule<T>::value_type;}) {
            //2. rule injected through an explicit specialization
            return custom_serialize_rule<T>{};
        } else if constexpr (requires {builtin::rule(ident);}) {
            //3. built-in rule, reached by qualified lookup only
            return builtin::rule(ident);
        }
        //otherwise returns void, which fails SerializeRuleExists below
    }
};

}

///Resolves the serialization rule for a type. Customize by declaring
///get_serialize_rule(std::type_identity<T>) in the namespace of T, or by
///specializing custom_serialize_rule<T>.
inline constexpr _cpo::GetRule get_rule = {};

template<typename T>
concept SerializeRuleExists = SerializeRule<decltype(get_rule(std::type_identity<T>{}))>;


template<typename T>
constexpr bool type_has_zero_fields = get_rule(std::type_identity<T>{}).field_count() == 0;


template<SerializeRuleExists T>
struct Layout: LayoutBase {
    static constexpr std::size_t field_count = get_rule(std::type_identity<T>{}).field_count();
    std::array<FieldDef, field_count> fields_arr = {};
    constexpr Layout() {
        this->fields = fields_arr;
        get_rule(std::type_identity<T>{}).init_layout(*this);
    }
};

template<SerializeRuleExists T>
constexpr auto layout_of_type = Layout<T>();

struct Schema {
    using Item = std::pair<std::string_view, const LayoutBase *>;
    std::vector<Item> schema = {};
    std::string_view root_type = {};
    
    constexpr static bool cmp_schema(const Item &a, const Item &b) {
        return a.first  < b.first;
    }

    template<SerializeRuleExists T>
    constexpr void recursive_walk() {
        const auto rule = get_rule(std::type_identity<T>{});
        rule.iterate_fields([&]<typename Ti>(Ti){
            using U = typename Ti::type;
            auto v = Item(type_name<U>,&layout_of_type<U>);
            auto pos = std::lower_bound(schema.begin(), schema.end(), v, cmp_schema);
            if (pos == schema.end() || pos->first != v.first) {
                schema.insert(pos, v);
                recursive_walk<U>();
            }
        });
    }

    template<SerializeRuleExists T>
    constexpr static Schema create() {
        Schema out;
        out.root_type = type_name<T>;
        out.schema.emplace_back(type_name<T>, &layout_of_type<T>);
        out.template recursive_walk<T>();
        return out;
    }

    constexpr std::size_t get_hash() const {
        std::size_t ret = 0;
        for (auto &[k, v]: schema) {
            ret = hash_combine(hash_combine(ret,fnv1a_hash(k)), v->get_hash());
        }
        return ret;
    }
};

template<typename T>
constexpr SchemaHash schema_hash = Schema::create<T>().get_hash();

template<BinaryReader T> 
class Deserializer {
public:
    constexpr Deserializer(T reader):reader(std::move(reader)) {}
    template<typename X, typename ... Args>
    constexpr void operator()(X &val, Args &&... ) {
        static_assert(SerializeRuleExists<X>, "No serialization rule for this type");
        auto rule = get_rule(std::type_identity<X>{});
        rule.deserialize(val, *this);
    }
    constexpr friend void read_binary(Deserializer &me, std::span<std::uint8_t> buffer) {
        me. reader(buffer);
    }

protected:
    T reader;
};

template<BinaryWriter T> 
class Serializer {
public:
    constexpr Serializer(T writer):writer(std::move(writer)) {}
    template<typename X, typename ... Args>
    constexpr void operator()(const X &val, Args &&...) {
        static_assert(SerializeRuleExists<X>, "No serialization rule for this type");
        auto rule = get_rule(std::type_identity<X>{});
        rule.serialize(val, *this);
    }
    constexpr friend void write_binary(Serializer &me, std::span<const std::uint8_t> buffer) {
        me. writer(buffer);
    }
protected:
    T writer;
};

constexpr auto string_deserializer(std::string_view str) {
    return Deserializer([str](std::span<std::uint8_t> buffer) mutable {
        if (buffer.size() > str.size()) throw std::runtime_error("Deserializer buffer overflow");
        std::copy_n(str.begin(), buffer.size(), buffer.begin());        
        str.remove_prefix(buffer.size());
    });
}

constexpr auto string_serializer(std::string &out) {
    return Serializer([&](std::span<const std::uint8_t> buffer) {
        for (auto &x: buffer) {
            out.push_back(static_cast<char>(x));
        }
    });
}

template< typename OutputType, SerializeRuleExists T, typename Iter>
constexpr auto serialize_to(const T &var, Iter iter) {
    Serializer srl([&](std::span<const std::uint8_t> buffer) {
        for(auto &x: buffer) {
            *iter++ = static_cast<OutputType>(x);
        }
    });
    srl(var);
    return iter;
}

template<SerializeRuleExists T, std::input_iterator Iter>
constexpr auto deserialize_from(Iter beg, Iter end, T &var) {
    Deserializer srl([&](std::span<std::uint8_t> buffer){
        for (auto &t: buffer) {
            if (beg == end) throw std::runtime_error("Deserialize input buffer overflow - data corrupted");
            t = static_cast<std::uint8_t>(*beg);
            ++beg;
        }
    });
    srl(var);
}


}





#if 0



namespace Layout {
    struct Type {
        std::string_view name;    
        auto fields(this auto &self) {
            return std::tie(self.name);
        }
        constexpr bool operator==(const Type &) const = default;
    };


    constexpr Type seq={"sequence"};
    ///type info for linear container, it deserializes as count (integral number) + content
    constexpr Type array={"array"};
    ///type info for map, it deserializes as count (integral number) + 2x content (key + value)
    constexpr Type map={"map"};
    ///type info for variant type, it deserializes as variant index (integral number) + content of selected variant. The schema contains all variants.
    constexpr Type variant={"variant"};
    ///type info for optional type, it deserializes as has_value (boolean) + content (if has_value is true). The schema contains only content.
    constexpr Type optional={"optional"};
    constexpr Type shared={"shared"};
    constexpr Type enumeration = {"enum"};
    constexpr Type empty = {"empty"};
    constexpr Type blob = {"blob"};
};


struct SerializerArchetype {
    
    template<typename T>
    void operator()(const T &val);
    template<typename T>
    void operator()(const T &val, std::string_view name);
    template<typename T>
    void operator()(const T &val, Layout::Type type);
    
    static constexpr bool reading = true;
    static constexpr bool writting = false;
    static constexpr bool gen_schema = false;
};


template<typename T>
concept Serializer = requires(T &ar, int v, double w, std::string s, std::string_view name, Layout::Type md) {
    {T::reading}->std::convertible_to<bool>;
    {T::writting}->std::convertible_to<bool>;
    {T::gen_schema}->std::convertible_to<bool>;
    {ar(v)};
    {ar(w)};
    {ar(s)};
    {ar(v,name)};
    {ar(w,name)};
    {ar(s,name)};
    {ar(v,md)};
    {ar(w,md)};
    {ar(s,md)};
};

static_assert(Serializer<SerializerArchetype>);

template<typename T, Serializer Ar>
void serialize_rule(T &val, Ar &ar) = delete;


///Used to deserialize derived class
/** Default implementation just emits T, but custom implementation can emit different type which is derived from T

How to use this function. Result of this function is an invocable template, which accepts archive and callback. The
function should be called for both serialize and deserialize. In case of serialize, it just require to store id
of the class to the archive. In case of deserialize, it reads id of the class from archive and calls callback
with selected type carried as type identity. The callback can create instance of type by using new or by using
a smart pointer. The result of the callback should be passed to result of the invocable
*/

template<typename T>
auto serialize_factory_rule(Layout::Type layout) {
    return [layout](Serializer auto &, auto cb) {        
        return cb(std::type_identity<T>(), layout);
    };
}


template<typename T>
concept HasSerializeRule = requires(T v, SerializerArchetype &ar) {
    {serialize_rule(v, ar)};
};

template<typename T>
concept HasSerializeMethod = requires(T v, SerializerArchetype &ar) {
    {v.serialize(ar)};
};

template<typename T>
concept HasSerializeFields = requires(T v) {
    {v.fields()}->IsTuple;
};

template<BinaryWriter WR, typename T>
requires(std::is_trivially_copyable_v<T>) 
constexpr void write_binary(WR &wr, const T &value) {
    auto binary_value = std::bit_cast<std::array<std::uint8_t, sizeof(T)> >(value);
    wr(binary_value);
    
}

template<BinaryReader RD, typename T>
requires(std::is_trivially_copyable_v<T>) 
constexpr void read_binary(RD &rd, T &value) {
    std::array<std::uint8_t, sizeof(T) > binary_value;
    rd(binary_value);
    value = std::bit_cast<T>(binary_value);
}      

template<typename T, BinaryWriter WR>
requires(std::is_arithmetic_v<T> && std::is_integral_v<T> && std::is_unsigned_v<T>)
constexpr void serialize_integer(T val, WR &wr) {
    std::array<uint8_t,20> buff;
    auto b = buff.begin();
    auto e = b;
    while (val > 127) {
        auto x = static_cast<std::uint8_t>((val & 0x7F) | 0x80);
        *e++ = x;
        val >>= 7;        
    }
    *e++ = static_cast<std::uint8_t>(val);
    wr(std::span<uint8_t>(b,e));
}

template<typename T, BinaryWriter WR>
requires(std::is_arithmetic_v<T> && std::is_integral_v<T> && std::is_signed_v<T>)
constexpr void serialize_integer(T val, WR &wr) {
    using UT = std::make_unsigned_t<T>;
    serialize_integer(((val < 0?static_cast<UT>(-val-1):static_cast<UT>(val)) << 1) | static_cast<UT>(val < 0?1:0), wr);
}

template<typename T, BinaryReader RD>
requires(std::is_arithmetic_v<T> && std::is_integral_v<T> && std::is_unsigned_v<T>)
constexpr void parse_integer(T &val, RD &rd) {
    val = 0;
    std::uint8_t c = 0;
    int shift = 0;
    do {
        rd(std::span<std::uint8_t>(&c,1));
        val |= static_cast<T>(static_cast<T>(c & 0x7F) << shift);
        shift += 7;
    } while (c & 0x80);
}

template<typename T, BinaryReader RD>
requires(std::is_arithmetic_v<T> && std::is_integral_v<T> && std::is_signed_v<T>)
constexpr void parse_integer(T &val, RD &rd) {
    using UT = std::make_unsigned_t<T>;
    UT v = 0;
    parse_integer(v, rd);    
    val = v & 0x1?-(static_cast<T>(v >> 1)+1):static_cast<T>(v>>1);    
}



template<BinaryWriter WR>
class BinarySerializer {
public:

    constexpr BinarySerializer(WR wr):_wr(wr) {}

    static constexpr bool reading = false;
    static constexpr bool writting = true;
    static constexpr bool gen_schema = false;

    template<typename T>
    constexpr BinarySerializer & operator()(const T &val) {
        if constexpr(std::is_invocable_v<T, BinarySerializer>) {
            val(*this);
        } else if constexpr(std::is_same_v<T, bool>) {
            write_binary(_wr, val);
        } else if constexpr(std::is_same_v<T, char>) {
            write_binary(_wr, val);
        } else if constexpr(std::is_integral_v<T>) {
            serialize_integer(val, _wr);
        } else if constexpr(std::is_floating_point_v<T>) {
            write_binary(_wr, val);
        } else if constexpr(std::is_same_v<T, std::string>) {
            std::size_t sz = val.size();
            serialize_integer(sz, _wr);
            if consteval {
                std::vector<std::uint8_t> bstr({val.begin(),val.end()});
                _wr(bstr);
            } else {
                std::span<const std::uint8_t> bstr(reinterpret_cast<const std::uint8_t *>(val.data()), val.size());
                _wr(bstr);
            }
        } else if constexpr(IsSharedPtr<T>) {
            using ET = typename std::decay_t<T>::element_type;
            if (!val) {
                serialize_integer(0U, _wr);
            } else {
                auto iter = _inst_table.find(val);
                if (iter != _inst_table.end()) {
                    serialize_integer(-iter->second, _wr);
                } else {
                    auto id = _next_id++;
                    _inst_table[val] = id;
                    serialize_integer(id, _wr);
                    serialize_factory_rule<ET>(Layout::shared)(*this,[](auto,auto){});
                    (*this)(*val);
                }
            }    
        } else if constexpr(IsTuple<T>) {
                std::apply([&](auto &... x){
                    ((*this)(x),...);
                },val);                    
        } else if constexpr(HasSerializeRule<T>) {
            serialize_rule(val, *this);
        } else if constexpr(HasSerializeMethod<T>) {
            val.serialize(*this);            
        } else if constexpr(HasSerializeFields<T>) {
            (*this)(val.fields());
        } else if constexpr(std::is_enum_v<T>) {
            serialize_integer(static_cast<std::intmax_t>(val), _wr)            ;
        } else if constexpr (std::is_empty_v<T>  || std::is_null_pointer_v<T>) {
            //empty
        } else {
            static_assert(std::is_trivially_copyable_v<T>, "No serialization rule, method or type is not trivially copyable");
            write_binary(_wr, val);
        }
        return *this;
    }

    template<typename T>
    constexpr BinarySerializer &operator()(const T &val, const std::string_view &) {
        return (*this)(val);
    }
    template<typename T>
    constexpr BinarySerializer &operator()(const T &val, const Layout::Type &) {
        return (*this)(val);
    }

protected:
    WR _wr;
    std::map<std::shared_ptr<void>, std::intmax_t> _inst_table;
    std::intmax_t _next_id = 1;
};

template<BinaryReader RD>
class BinaryParser {
public:

    constexpr BinaryParser(RD rd):_rd(rd) {}

    static constexpr bool reading = true;
    static constexpr bool writting = false;
    static constexpr bool gen_schema = false;


    template<typename T>
    constexpr BinaryParser &operator()(T &val) {
        if constexpr(std::is_invocable_v<T, BinaryParser>) {
            val(*this);
        } else if constexpr(std::is_same_v<T, bool>) {
            read_binary(_rd, val);
            val = !!val; //normalize
        } else if constexpr(std::is_same_v<T, char>) {
            read_binary(_rd, val);
        } else if constexpr(std::is_integral_v<T>) {
            parse_integer(val, _rd);
        } else if constexpr(std::is_floating_point_v<T>) {
            read_binary(_rd, val);
        } else if constexpr(std::is_same_v<T, std::string>) {
            std::size_t sz;
            parse_integer(sz, _rd);
            val.resize(sz);
            if consteval {
                std::vector<std::uint8_t> buffer;
                buffer.resize(sz);
                _rd(buffer);
                std::copy(buffer.begin(), buffer.end(), val.begin());
            } else {
                std::span<std::uint8_t> buffer(reinterpret_cast<std::uint8_t *>(val.data()), val.size());
                _rd(buffer);
            }
        } else if constexpr(IsSharedPtr<T>) {
            std::intmax_t r;
            using ET = typename std::decay_t<T>::element_type;
            parse_integer(r, _rd);
            if (r>0) {
                auto sch = serialize_factory_rule<ET>(Layout::shared)(*this,[&]<typename TI>(TI, auto){
                    using ST = typename TI::type;
                    auto sch =  std::make_shared<ST>();
                    (*this)(*sch);
                    return sch;
                });
                _inst_table[r] = sch;
                val = std::static_pointer_cast<ET>(sch);                
            } else if (r < 0) {
                auto iter = _inst_table.find(-r);
                if (iter == _inst_table.end()) throw std::runtime_error("Reference to undefined instance, corrupted archive");
                val = std::static_pointer_cast<ET>(iter->second);
            } else {
                val = {};
            }
        
        } else if constexpr(IsTuple<T>) {
                std::apply([&](auto &... x){
                    ((*this)(x),...);
                },val);                    
        } else if constexpr(HasSerializeRule<T>) {
            serialize_rule(val, *this);
        } else if constexpr(HasSerializeMethod<T>) {
            val.serialize(*this);            
        } else if constexpr(HasSerializeFields<T>) {
            auto flds =  val.fields();
            (*this)(flds);
        } else if constexpr(std::is_enum_v<T>) {
            std::intmax_t idx;
            parse_integer(idx, _rd)            ;
            val = static_cast<T>(idx);
        } else if constexpr (std::is_empty_v<T>  || std::is_null_pointer_v<T>) {
            //empty
        } else {
            static_assert(std::is_trivially_copyable_v<T>, "No serialization rule, method or type is not trivially copyable");            
            read_binary(_rd, val);
        }
        return *this;
    }
    template<typename T>
    constexpr BinaryParser & operator()( T &val, const std::string_view &) {        
        return (*this)(val);
    }
    template<typename T>
    constexpr BinaryParser & operator()( T &val, const Layout::Type &) {
        return (*this)(val);
    }


protected:

    RD _rd;
    std::unordered_map<std::intmax_t, std::shared_ptr<void> > _inst_table;    
};

template<typename T> constexpr std::string_view schema_type_name = type_name<T>;

class BinarySchemaGenerator {
public:

    struct NameAndType {
        std::string type;
        std::optional<std::string> name = {};

        constexpr void serialize(this auto &self, auto &ar) {
            ar(self.type, "type");
            ar(self.name, "name");
        }
        constexpr std::size_t get_hash() const {
            return fnv1a_hash(type);
        }
    };

    struct TypeDesc {
        std::optional<Layout::Type> layout;       //generic type 
        std::optional<std::size_t> size;                 //size if it is binary blob
        std::vector<NameAndType> fields;                 //fields

        constexpr void serialize(this auto &self, auto &ar) {
            ar(self.layout, "layout");
            ar(self.size, "size");
            ar(self.fields, "fields");            
        }
        constexpr std::size_t get_hash() const {
            std::size_t seed =0;
            for (auto &x: fields) {
                seed = hash_combine(seed, x.get_hash());
            };
            if (layout) seed = hash_combine(seed, 

        }
    };

    static constexpr bool reading = false;
    static constexpr bool writting = true;
    static constexpr bool gen_schema = true;

    using SchemaMap = std::unordered_map<std::string, TypeDesc>;
    struct Schema {
        SchemaMap map;
        std::string root;

        void serialize(this auto &self, auto &ar) {
            ar(self.root, "root");
            ar(self.map, "map");
        }
    };



    ///schema item / type info / f
    template<typename T>
    void build_schema() {       
        SchemaMap tmp_map;
        auto ins = tmp_map.emplace("root", TypeDesc({},{},{}));
        _stack.push_back(ins.first);
        (*this)(T());
        _schema.root = ins.first->second.fields.back().type;
    }

    template<typename T>
    constexpr BinarySchemaGenerator &operator()(const T &val) {
        if constexpr(std::is_same_v<T, bool>) {
            top().push_back({"bool", std::move(_cur_name)});
        } else if constexpr(std::is_same_v<T, char>) {
            top().push_back({"char", std::move(_cur_name)});
        } else if constexpr(std::is_integral_v<T>) {
            if constexpr(std::is_unsigned_v<T>) {
                top().push_back({"unsigned_varint", std::move(_cur_name)});
            } else{
                top().push_back({"signed_varint", std::move(_cur_name)});
            }
        } else if constexpr(std::is_same_v<T, float>) {
                top().push_back({"float",_cur_name});
        } else if constexpr(std::is_floating_point_v<T>) {
                top().push_back({"double", std::move(_cur_name)});
        } else if constexpr(std::is_same_v<T, std::string>) {
                top().push_back({"string", std::move(_cur_name)});
        } else if constexpr(IsTuple<T>) {
                std::apply([&](auto &... x){
                    ((*this)(x),...);
                },val);                    
        } else {
            std::string tname ( schema_type_name<T>);
            top().push_back({tname, std::move(_cur_name)});
            _cur_name.reset();
            auto iter = _schema.map.find(tname);
            if (iter == _schema.map.end()) {
                auto ins = _schema.map.emplace(std::move(tname), 
                            TypeDesc{Layout::seq,{},{}});
                iter = ins.first;
                _stack.push_back(iter);
                if constexpr(HasSerializeRule<T>) {
                    serialize_rule(val, *this);
                } else if constexpr (HasSerializeMethod<T>) {
                    val.serialize(*this);
                } else if constexpr (HasSerializeFields<T>) {
                    auto t = val.fields();
                    (*this)(t);
                } else if constexpr(std::is_enum_v<T>) {
                    auto &b = _stack.back()->second;
                    b.layout = Layout::enumeration;
                } else if constexpr (std::is_empty_v<T> || std::is_null_pointer_v<T>) {
                    auto &b = _stack.back()->second;
                    b.size = 0;
                    b.layout = Layout::empty;
                } else {
                    static_assert(std::is_trivially_copyable_v<T>, "No serialization rule, method or type is not trivially copyable");
                    auto &b = _stack.back()->second;
                    b.size = sizeof(T);
                    b.layout = Layout::blob;
                }
                _stack.pop_back();
            }
        }        
        return *this;
    }

    template<typename T>
    BinarySchemaGenerator &operator()(const T &val, std::string_view name) {
        _cur_name.emplace(name);
        (*this)(val);
        _cur_name.reset();
        return *this;
    }

    template<typename T>
    BinarySchemaGenerator &operator()(const T &val, Layout::Type md) {
        (*this)(val);
        _stack.back()->second.layout = md;
        return *this;
    }


    const Schema &get_schema() const {return _schema;}

protected:

    using SchemaIter = SchemaMap::iterator;
    Schema _schema;
    std::vector<SchemaIter> _stack;
    std::optional<std::string> _cur_name;

    std::vector<NameAndType> &top() {
        return _stack.back()->second.fields;
    }

};


template<typename T, Serializer Ar>
requires (IsLinearContainer<std::decay_t<T> > )
void serialize_rule(T &val, Ar &ar) {
    if constexpr (Ar::gen_schema) {
        typename T::value_type v ={};
        ar(v, Layout::array);
    } else {
        std::size_t sz = val.size();
        ar(sz);
        if constexpr (Ar::reading) {
            val.resize(0);
            val.resize(sz);
        }
        for (auto &x: val) {
            ar(x);
        }
    }
};

template<IsOptional T, Serializer Ar>
void serialize_rule(T &val, Ar &ar) {
    if constexpr (Ar::gen_schema) {
        typename T::value_type v = {};
        ar(v, Layout::optional);
    } else {
        bool b = val.has_value();
        ar(b);
        if constexpr (Ar::reading) {
            val.reset();
            if (b) val.emplace();
        }
        if (b) {
            ar(val.value());
        }
    }
}


template<IsVariant T, Serializer Ar>
void serialize_rule(T &val, Ar &ar) {
    using DT = VariantDecay<T>;
    if constexpr (Ar::gen_schema) {     
        ([&]<size_t ... I>(std::index_sequence<I...>){
            auto t = std::tuple<std::variant_alternative_t<I, DT>...>();
            ar(t, Layout::variant);
        })(std::make_index_sequence<std::variant_size_v<DT> >());        
    } else {
        std::size_t var =val.index();
        ar(var);
        if constexpr(Ar::reading) {
            ([&]<size_t ... I>(std::index_sequence<I...>){
                return ((I == var?(val.template emplace<I>(),true):false) || ...); //return value is ignored
            })(std::make_index_sequence<std::variant_size_v<DT> >());
        }
        std::visit([&](auto &v){
            ar(v);
        }, val);
    }
}


template<typename T, Serializer Ar> 
requires (IsMap<std::decay_t<T> > && !IsLinearContainer<std::decay_t<T> > )
void serialize_rule(T &val, Ar &ar) {
    if constexpr (Ar::gen_schema) {
        std::tuple<typename T::key_type, typename T::mapped_type> flds;
        ar(flds, Layout::map);
    } else {
        std::size_t sz = val.size();
        ar(sz);
        if constexpr (Ar::reading) {
            val.clear();
            for (std::size_t i = 0; i < sz; ++i) {
                typename T::key_type key = {};
                typename T::mapped_type value = {};
                ar(key);
                ar(value);
                val.emplace(std::move(key), std::move(value));
            }
        } else {
            for (const auto &[k, v]: val) {
                ar(k);
                ar(v);
            }
        }
    }
}

template<IsPair T, Serializer Ar>
void serialize_rule(T &val, Ar &ar) {
    ar(val.first);
    ar(val.second);
}

template<typename T, Serializer Ar>
void serialize_rule(const std::unique_ptr<T> &val, Ar &ar) {
    if constexpr(Ar::gen_schema) {
        serialize_factory_rule<T>(Layout::optional)(ar, [&]<typename TI>(TI, Layout::Type layout){
            using TU = typename TI::type;
            ar(TU(), layout);
        });
    } else {
        bool b = !!val;
        ar(b);
        if (b) {
            serialize_factory_rule<T>(Layout::optional)(ar, [&]<typename TI>(TI, Layout::Type layout){
                using TU = typename TI::type;
                if constexpr(Ar::gen_schema) {
                    ar(TU(), layout);
                } else {
                    if (b) ar(*static_cast<const TU *>(val.get()));
                }
            });    

        }
    }


}
template<typename T, Serializer Ar>
void serialize_rule( std::unique_ptr<T> &val, Ar &ar) {
    bool b;
    ar(b);
    if (b) {
        val = serialize_factory_rule<T>(Layout::optional)(ar, [&]<typename TI>(TI, Layout::Type ){
            using TU = typename TI::type;
            auto ptr = std::make_unique<TU>();    
            ar(*ptr);
            return ptr;
        });
    } else {
        val = {};
    }
}

template<typename T, Serializer Ar>
void serialize_rule(const std::shared_ptr<T> &, Ar &ar) {
    static_assert(Ar::gen_schema, "this rule is only for schema");
    serialize_factory_rule<T>(Layout::shared)(ar, [&]<typename TI>(TI, Layout::Type layout){
        using TU = typename TI::type;
        ar(TU(), layout);
    });
}
template<typename T, Serializer Ar>
void serialize_rule( std::shared_ptr<T> &val, Ar &ar) {
    reference(ar,val);
}


struct SerializeNoContent {
    constexpr void serialize(auto &) {}
};


template<std::size_t N>
struct SerializeBinaryBlob {
    std::array<std::uint8_t,N> data;
    constexpr SerializeBinaryBlob()  = default;
    constexpr SerializeBinaryBlob(const auto &val) : data(std::bit_cast<std::array<std::uint8_t,N> >(val)) {}
    
    template<typename T>
    constexpr void extract(T &to) const {
        to = std::bit_cast<T>(data);
    }

    template<typename T>
    constexpr T extract() const{
        return std::bit_cast<T>(data);
    }
};

template<typename T>
SerializeBinaryBlob(const T &val) -> SerializeBinaryBlob<sizeof(T)>;

static_assert(Serializer<BinarySchemaGenerator>);
#endif

