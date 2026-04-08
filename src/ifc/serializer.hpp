

#include <concepts>
#include <functional>
#include <iterator>
#include <span>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

template<typename X>
concept Serialize_IsTuple = requires { typename std::tuple_size<X>::type; };

template<typename X>
concept Serialize_HasFields = requires(X val) {
    {val.fields()} -> Serialize_IsTuple;
};

struct AbstractSerializer {
    template<typename X>
    void write(const X &);
    template<typename X>
    void write_span(std::span<X>);
    template<typename X>
    void read(X &);
    template<typename X>
    void read_span(std::span<X>);

};

template<typename X>
concept Serialize_HasRule = requires(X val, AbstractSerializer ser) {
    {serialize_rule_write(val, ser)};
    {serialize_rule_read(val, ser)};
};




template<std::invocable<const void *, std::size_t> _Output>
class BinaryWriter {
public:
    BinaryWriter(_Output iter):_iter(std::move(iter)) {}

    template<typename Field>
    void write(const Field &v) {
        if constexpr(Serialize_HasFields<Field>) {
            write(v.fields());
        } else if constexpr(Serialize_IsTuple<Field>) {
             ([&]<typename std::size_t ... N>(std::index_sequence<N...>) {
                (write(std::get<N>(v)),...);
            })(std::index_sequence_for<Field>{});
        } else if constexpr(Serialize_HasRule<Field>) {
            serialize_rule_write(v, *this);
        } else  {
            static_assert(std::is_trivially_copy_assignable_v<Field>, "Field is not trivial, you need to declare serialization rule");
            std::invoke(_iter,&v, sizeof(v));
        } 
    }
    template<typename Field>
    void write_span(std::span<Field> span) {
        if constexpr(std::is_trivially_copy_assignable_v<std::remove_const_t<Field> >) {
            std::invoke(_iter, span.data(), sizeof(Field) * span.size());
        } else {
            for (const auto &x: span) write(x);
        }
    }
protected:
    _Output _iter;
};

template<std::invocable<void *, std::size_t> _Input>
class BinaryReader {
public:
    BinaryReader(_Input iter):_iter(std::move(iter)) {}

    template<typename Field>
    void read(Field &v) {
        if constexpr(Serialize_HasFields<Field>) {
            auto f = v.fields();
            read(f);
        } else if constexpr(Serialize_IsTuple<Field>) {
             ([&]<typename std::size_t ... N>(std::index_sequence<N...>) {
                (read(std::get<N>(v)),...);
            })(std::index_sequence_for<Field>{});
        } else if constexpr(Serialize_HasRule<Field>) {
            serialize_rule_read(v, *this);
        } else  {
            static_assert(std::is_trivially_copy_assignable_v<Field>, "Field is not trivial, you need to declare serialization rule");
            std::invoke(_iter, &v, sizeof(v));
        } 
    }
    template<typename Field>
    void read_span(std::span<Field> span) {
        if constexpr(std::is_trivially_copy_assignable_v<Field>) {
            std::invoke(_iter, span.data(), sizeof(Field) * span.size());
        } else {
            for (const auto &x: span) write(x);
        }
    }
protected:
    _Input _iter;
};

template<typename T>
concept Serialize_IsLinearContainer = requires(T v, std::size_t sz) {
    {v.size()} -> std::convertible_to<std::size_t>;
    {v.resize(sz)};
    {v.begin()} -> std::input_or_output_iterator;
    {v.end()} -> std::input_or_output_iterator;
};

template<Serialize_IsLinearContainer T, typename Serializer>
void serialize_rule_write(const T &v, Serializer &sr) {
    std::size_t sz = v.size();
    sr.write(sz);
    sr.write_span(std::span<const typename T::value_type>(v));
}
template<Serialize_IsLinearContainer T, typename Serializer>
void serialize_rule_read(T &v, Serializer &sr) {
    size_t sz;
    sr.read(sz);
    v.resize(sz);
    sr.read_span(std::span<typename T::value_type>(v));
}

inline auto serialize_to_string(std::string &output) {
    return [&output](const void *ptr, std::size_t sz) {
        output.append(reinterpret_cast<const char *>(ptr), sz);
    };
}

inline auto deserialize_from_string(std::string_view input) {
    return [input, pos = std::size_t(0)](void *ptr, std::size_t sz) mutable {
        if (pos + sz > input.size()) throw std::runtime_error("Serialization failed, stream is shorter");
        std::copy_n(input.data()+pos, sz, reinterpret_cast<char *>(ptr));
        pos += sz;
    };
}

inline void test () {
    AbstractSerializer s;
    std::string t;
    serialize_rule_read(t, s);
}

static_assert(Serialize_IsLinearContainer<std::string>);
static_assert(Serialize_HasRule<std::string>);

template<typename X>
inline std::string to_binary_blob(const X &v) {
    std::string out;
    BinaryWriter wr(serialize_to_string(out));
    wr.write(v);
    return out;
}

template<typename T>
inline T from_binary_blob(std::string_view data) {
    T out;
    BinaryReader rd(deserialize_from_string(data));
    rd.read(out);
    return out;

}
