#pragma once
#include "common.h"
#include "wrapper.h"
#include <typeinfo>
#include <sstream>

namespace quarkbot {

enum class MarketEventType {
    unknown,
    ///tick data (L1)
    tickdata,
    ///full orderbook
    orderbook,
    ///(not supported yet) auction data (Stocks)
    auction,
    ///(not supported yet) liquidation stream
    liquidations,
    ///(not supported yet) underlying index
    index,
    ///(not supported yet) perpetual contract funding
    funding,
    ///(not supported yet) instrument news
    news,
    ///(not supported yet) contract expiration, related contract listing
    contracts
};

inline constexpr std::string_view get_string(MarketEventType e) {
    switch (e) {
        case MarketEventType::tickdata: return "L1 tick data";
        case MarketEventType::orderbook: return "L2 orderbook";
        case MarketEventType::auction: return "auction";
        case MarketEventType::liquidations: return "liquidations";
        case MarketEventType::index: return "index";
        case MarketEventType::funding: return "funding";
        case MarketEventType::news: return "news";
        case MarketEventType::contracts: return "contracts";
        default: return "unknown";
    }
}

inline constexpr bool can_collapse(MarketEventType e) {
    return e == MarketEventType::tickdata || e == MarketEventType::orderbook || e == MarketEventType::auction
        || e == MarketEventType::index || e == MarketEventType::funding;

}

///Contains abstract interface for market event
class IMarketEvent {
public:
    virtual ~IMarketEvent() = default;
    ///Retrieve value directly
    /**
     * @param type type of target variable
     * @param ptr pointer to target variable
     * @param sz size of target variable
     * @retval true stored
     * @retval false type mismatch
     * the implementation must check type and size of the variable. Only if both are correct,
     * it should use reinterpret_cast<> to convert ptr to typed ptr and use assignment to
     * store value
     */
    virtual bool retrieve_value(const std::type_info &type, void *ptr, std::size_t sz) const = 0;
    ///Retrieve as optional
    /**
     * @param type type of content (T in std::optional<T>)
     * @param ptr pointer to variable of type std::optional<T>. The variable should be empty
     * @param sz size of variable of type std::optional<T>
     * the implementation must check type and size of the variable. Only if both are correct,
     * it should use reinterpret_cast<> to convert ptr to std::optional<T> and use emplace() to
     * store value
     */
    virtual void retrieve_optional(const std::type_info &type, void *ptr, std::size_t sz) const = 0;
    ///Tests whether given type can be retrieved
    /**
     * @param type type to retrieve
     * @retval true can be retrieved
     * @retval false cannot be retrieved
     */
    virtual bool contains(const std::type_info &type) const = 0;
    ///Retrieves associated MarketEventTyoe
    virtual MarketEventType type() const = 0;
    ///Dump content
    /**
     * @param out output stream
     */
    virtual void dump(std::ostream &out) const = 0;
    class Null;
};

///Contains undefined market event
class IMarketEvent::Null: public IMarketEvent {
public:
    virtual bool retrieve_value(const std::type_info &, void *, std::size_t ) const override {return false;};
    virtual void retrieve_optional(const std::type_info &, void *, std::size_t ) const override {};
    virtual bool contains(const std::type_info &) const override {return false;}
    virtual MarketEventType type() const override {return MarketEventType::unknown;}
    virtual void dump(std::ostream &) const override {};
};

///Contains market event
/**
 * This class handles an access to a market event. It can hold any market event type and its data.
 */
class MarketEvent: public Wrapper<IMarketEvent> {
public:
    using Wrapper<IMarketEvent>::Wrapper;

    ///Store market event into a variable
    /**
     * @param v a variable of suitable type to accept data of the market event. For example
     * tickdata requires TickData type, orderbook requires OrderBook type
     *
     * @retval true received
     * @retval false unsupported type (passed variable had different type)
     */
    template<typename T>
    bool get(T &v) const {
        return _ptr->retrieve_value(typeid(T), &v, sizeof(T));
    }
    ///Retrieve data as optional variable
    /**
     * @tparam T type
     *
     * @code
     * std::optional<TickData> ticker = market_event;
     * if (ticker) {
     *      //process ticker
     * }
     * @endcode
     *
     * Function returns empty optional variable if its type is not compatible with the market event
     */
    template<typename T>
    operator std::optional<T>() const {
        std::optional<T> ret;
        _ptr->retrieve_optional(typeid(T), &ret, sizeof(ret));
        return ret;
    }

    ///Tests whether event contains given type
    /**
     * @tparam T type to test (for example OrderBook)
     * @retval true market even contains such information
     * @retval false market event doesn't contain such information
     */
    template<typename T>
    bool contains() const {
        return _ptr->contains(typeid(T));
    }

    ///Retrieve associated MarketEventType
    /**
     * @return stored type.
     * @note you should expect, that associated structure should match with returned type. For
     * example if MarketEventType::tickdata is returned, then TickData can be retrieved from the
     * object
     */
    MarketEventType get_type() const {
        return _ptr->type();
    }

    ///Dump content of the market event (for debugging purpose)
    friend std::ostream &operator<<(std::ostream &s, const MarketEvent &ev) {
        ev._ptr->dump(s);
        return s;
    }
};


///Helps to create MarketEvent object as a holder
/**
 * The holder can be created for one time use, or can be created as storage. One time usage
 * allows to create an instance and broadcast it as event. Storage works differently. It
 * is allocated once, and its instance is repeatedly broadcasted. Its content is replaced
 * with each new event. This is useful for collapsable events (such events, which can be collapsed
 * in case that they are generated too fast). In this case, the strategy actually retrieves
 * the lastest value regardless on how long the event was delayed in the queue
 *
 * @tparam _type specifies MarketEventType which is held by this object
 * @tparam T contains type which holds market data
 * @tparam Lock specifies lock used to access the data. For one time use, there can be NoLock
 * as it is excepted, that market data cannot change during broadcasting. For storage mode,
 * you should specify std::mutex
 */
template<MarketEventType _type, typename T, typename Lock = NoLock>
class MarketEventHolder: public IMarketEvent {
public:
    ///construct initial instance
    template<typename ... Args> requires(std::is_constructible_v<T, Args...>)
    MarketEventHolder(Args &&... args):_val(std::forward<Args>(args)...) {}
    ///replace value
    template<typename ... Args> requires(std::is_constructible_v<T, Args...>)
    void emplace(Args && ... args) {
        std::lock_guard _(_mx);
        _val = T(std::forward<Args>(args)...);
    }
    ///replace value
    void set(const T &val) {
        std::lock_guard _(_mx);
        _val = val;
    }
    ///replace value
    void set(T &&val) {
        std::lock_guard _(_mx);
        _val = std::move(val);
    }

    virtual bool retrieve_value(const std::type_info &type, void *ptr, std::size_t sz) const override {
        if (type == typeid(T) && sz == sizeof(T)) {
            std::lock_guard _(_mx);
            T *target = reinterpret_cast<T *>(ptr);
            *target = _val;
            return true;
        }
        return false;
    }
    virtual void retrieve_optional(const std::type_info &type, void *ptr, std::size_t sz) const override {
        if (type == typeid(T) && sz == sizeof(std::optional<T>)) {
            std::lock_guard _(_mx);
            auto target = reinterpret_cast<std::optional<T> *>(ptr);
            target->emplace(_val);
        }

    }
    virtual bool contains(const std::type_info &type) const override {
        return type == typeid(T);
    }

    virtual MarketEventType type() const override {return _type;}

    ///create instance
    template<typename ... Args> requires(std::is_constructible_v<T, Args...>)
    static std::shared_ptr<MarketEventHolder> create(Args && ... args) {
        return std::make_shared<MarketEventHolder>(std::forward<Args>(args)...);
    }

    virtual void dump(std::ostream &s) const override {
        if constexpr(can_output_to_ostream<T>) {
            std::lock_guard _(_mx);
            s << _val;
        } else {
            s << "<" << typeid(T).name() << ">";
        }
    };


protected:
    [[no_unique_address]] mutable Lock _mx;
    T _val;
};


}
