
#pragma once
#include <bit>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include "type_name.hpp"

template<typename T> concept BinaryReader = std::is_invocable_v<T, std::span<std::uint8_t> >;
template<typename T> concept BinaryWriter = std::is_invocable_v<T, std::span<const std::uint8_t> >;    

namespace srl { 

template<typename T>
concept IsOptional = requires(T v) {
    bool(v);          // konverze na bool
    *v;               // dereference
    v.has_value();    // metoda has_value
};

template<typename ... Args>
inline constexpr std::true_type test_is_variant(const std::variant<Args...> &) {return {};}
inline constexpr std::false_type test_is_variant(...)  {return {};}
template<typename ... Args>
inline constexpr std::true_type test_is_tuple(const std::tuple<Args...> &) {return {};}
inline constexpr std::false_type test_is_tuple(...)  {return {};}

template<typename T>
inline constexpr std::true_type test_is_shared_ptr(const std::shared_ptr<T> &) {return {};}
inline constexpr std::false_type test_is_shared_ptr(...) {return {};}

template<typename A, typename B>
inline constexpr std::true_type test_is_pair(const std::pair<A,B> &) {return {};}
inline constexpr std::false_type test_is_pair(...) {return {};}

template<typename ... Args>
inline constexpr std::variant<Args ...> variant_decay_hlp(const std::variant<Args...> &);

template<typename T>
using VariantDecay = decltype(variant_decay_hlp(std::declval<T>()));


template<typename T>
concept IsVariant = decltype(test_is_variant(std::declval<T>()))::value;
template<typename T>
concept IsTuple = decltype(test_is_tuple(std::declval<T>()))::value;
template<typename T>
concept IsSharedPtr = decltype(test_is_shared_ptr(std::declval<T>()))::value;
template<typename T>
concept IsPair =  decltype(test_is_pair(std::declval<T>()))::value;

template<typename T>
concept IsLinearContainer = requires(T v, typename T::value_type val, std::size_t sz) {
    { v.begin() } -> std::input_or_output_iterator;
    { v.end() } -> std::input_or_output_iterator;
    { v.resize(sz) };
};

template<typename T>
concept IsMap = requires(T v) {
    typename T::key_type;
    typename T::mapped_type;
    { v.begin()->first };
    { v.begin()->second };
    { v.clear() };
} && requires(T v, typename T::key_type k, typename T::mapped_type m) {
    v.emplace(k, m);
};

template<typename T> struct IsTimePointHlp: std::integral_constant<bool, false> {};
template<typename T> struct IsTimePointHlp<std::chrono::time_point<T> >: std::integral_constant<bool, true> {};
template<typename T> concept IsTimepoint = IsTimePointHlp<T>::value;





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
    std::uint8_t c;
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

    BinarySerializer(WR wr):_wr(wr) {}

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

    BinaryParser(RD rd):_rd(rd) {}

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

    
/*

    struct NamePrefix {
        std::string name;
    };

    struct TypePrefix {
        std::string name;
    };
    
    struct SizePrefix {
        std::size_t size;
    };

    struct SchemaItem {
        std::variant<std::monostate, NamePrefix, TypePrefix, SizePrefix> prefix;
        std::vector<SchemaItem> content = {};
    };
*/

    struct NameAndType {
        std::string type;
        std::optional<std::string> name = {};

        void serialize(this auto &self, auto &ar) {
            ar(self.type, "type");
            ar(self.name, "name");
        }
    };

    struct TypeDesc {
        std::optional<Layout::Type> layout;       //generic type 
        std::optional<std::size_t> size;                 //size if it is binary blob
        std::vector<NameAndType> fields;                 //fields

        void serialize(this auto &self, auto &ar) {
            ar(self.layout, "layout");
            ar(self.size, "size");
            ar(self.fields, "fields");
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
    BinarySchemaGenerator &operator()(const T &val) {
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

}