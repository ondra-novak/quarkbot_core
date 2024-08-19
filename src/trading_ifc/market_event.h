#pragma once
#include "common.h"
#include <typeinfo>
#include <sstream>

namespace trading_api {



class InvalidType : public std::exception {
public:
    virtual const char *what() const noexcept {return "InvalidType: Variable contains different type";}
};

///Type erased reference
class AnyRef {
public:

    ///construct any reference
    template<typename T>
    AnyRef(const T &val):_ti(&typeid(T)),_ptr(&val),_print(&print_fn<T>) {}

    AnyRef(const AnyRef &) = delete;
    AnyRef &operator=(const AnyRef &) = delete;

    ///test, whether the reference contains this type
    template<typename T>
    bool is() const {
        return typeid(T) == *_ti;
    }

    ///retrieve as type
    template<typename T>
    const T &get() const {
        if (!is<T>()) throw InvalidType();
        return *reinterpret_cast<const T *>(_ptr);
    }

    ///copy to variable
    /**
     * @param ref variable where value is copied
     * @retval true copied
     * @retval false different type
     */
    template<typename T>
    bool get(T &ref) const {
        if (!is<T>()) return false;
        ref = *reinterpret_cast<const T *>(_ptr);
        return true;
    }

    ///direct convert to T
    template<typename T>
    operator const T &() const {
        return get<T>();
    }

    ///calls lambda if its argument matches to stored type
    template<lambda_with_1arg Fn>
    bool operator>>(Fn &&fn) {
        using T = std::decay_t<typename _details::DetectFnFirstArg<std::decay_t<Fn> >::type>;
        if (is<T>()) {
            fn(get<T>());
            return true;
        }
        return false;
    }

    ///to stream
    friend std::ostream &operator<<(std::ostream &other, const AnyRef &ref) {
        if (ref._print) {
            ref._print(other, ref._ptr);
        } else {
            other << ref._ti->name();
        }
        return other;
    }

    ///retrieves internal value as string
    std::string to_string() const {
        std::ostringstream str;
        str << *this;
        return str.str();
    }

protected:

    const std::type_info *_ti;
    const void *_ptr;
    void (*_print)(std::ostream &out, const void *);



    template<typename T>
    static void print_fn(std::ostream &out, const void *ptr) {
        out << type_to_string<T>;
        if constexpr(can_output_to_ostream<T>) {
             const T *val = reinterpret_cast<const T *>(ptr);
             out << "(" << *val << ")";
        }
    }
};

class MarketEvent: public AnyRef {
public:
    template<typename T>
    MarketEvent(SubscriptionType type, const T &val)
        :AnyRef(val), _type(type) {}

    ///Retrieve subscription type
    SubscriptionType get_type() const {return _type;}

protected:
    SubscriptionType _type;
};

template<typename T>
class MarketData {
public:
    
    using value_type = T;

    template<std::invocable<void *, std::size_t> Fn>
    MarketData(Fn &&fn) {
        available = fn(&data, sizeof(T));
    }
    MarketData(const MarketData &other):available(other.available) {
        if (available) std::construct_at(&data, other.data);
    }
    MarketData(MarketData &&other):available(other.available) {
        if (available) std::construct_at(&data, std::move(other.data));
    }
    ~MarketData() {
        if (available) std::destroy_at(&data);
    }

    MarketData &operator=(const MarketData &other) {
        if (this != &other) {
            if (available) std::destroy_at(&data);
            available = other.available;
            if (available) std::construct_at(&data, other.data);
        }
    }
    MarketData &operator=(MarketData &&other) {
        if (this != &other) {
            if (available) std::destroy_at(&data);
            available = other.available;
            if (available) std::construct_at(&data,std::move(other.data));
        }
    }



    explicit operator bool() const {return available;}
    bool has_value() const {return available;}
    T *operator->() {return &data;}
    const T *operator->() const {return &data;}
    T &operator *() {return data;}
    const T &operator *() const {return data;}


protected:
    bool available = false;
    union {
        T data;
    };
};

class IMarketEvent {
public:
    virtual ~IMarketEvent() = default;
    virtual bool retrieve(const std::type_info &type, void *ptr, std::size_t sz) const = 0;
    virtual bool contains(const std::type_info &type) const = 0;

    class Null;
};

class IMarketEvent::Null: public IMarketEvent {
public:
    virtual bool retrieve(const std::type_info &type, void *ptr, std::size_t sz) const {return false;};
    virtual bool contains(const std::type_info &type) const {return false;}
};

}
