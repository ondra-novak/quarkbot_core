#pragma once

#include "instrument.h"
#include "account.h"
#include "common.h"

#include <memory>
#include <variant>
#include <optional>


namespace quarkbot {

class Instrument;

struct SerializedOrder{
    std::string order_id;   //order unique identifier
    std::string order_content;  //binary content of the order

    friend bool unused(const SerializedOrder &ord) {
        return ord.order_id.empty() && ord.order_content.empty();
    }
};

class IOrder {
public:

    enum class State {
        ///order is not defined
        undefined,
        ///order object is associated with an instrument, but doesn't represent any real other
        associated,
        ///validation failed
        discarded,
        ///rejected by exchange
        rejected,
        ///order is being sent to the exchange (state is not yet known)
        sent,
        ///order is waiting to be triggered (stop order)
        waiting,
        ///order is active (in orderbook)
        active,
        ///order has been canceled
        canceled,
        ///order has been filled
        filled,
        ///inital state for restored order before its final state is restored
        restoring
    };


    static bool is_done(State st) {
        return st != State::sent
                && st != State::active
                && st != State::waiting
                && st != State::restoring;
    }



    class Reason {
    public:
        enum E {
            ///no error reported
            no_reason,
            ///reason of cancel is replace
            replace,
            ///order to replace not found, or already filled
            not_found,
            ///discarded because position would be out of limit
            position_limit,
            ///discarded because max leverage would be reached
            max_leverage,
            ///rejected during replace because there is unprocessed fill on way
            unprocessed_fill,
            ///discarded because invalid params
            invalid_params,
            ///discarded because order used in call is not compatible (dynamic cast failed)
            incompatible_order,
            ///discarded because invalid usage of amend
            invalid_amend,
            ///discarded because unsuppored by service provider
            unsupported,
            ///no funds on exchange
            no_funds,
            ///post only order would cross
            crossing,
            ///error reported on exchange
            exchange_error,
            ///any other internal error
            internal_error,
            ///trading is not possible, because low liquidity - (trading was stopped)
            low_liquidity,
            ///order rejected because exchange is overloaded
            exchange_overload,
            ///order amount is too small
            too_small,
            ///unknown reason
            unknown
        };
        ///default initialize with no reason
        Reason():_reason(no_reason) {}
        ///initialize with reason
        Reason(E val):_reason(val) {}
        ///initialize with reason, attach message
        Reason(E val, std::string_view msg)
            :_reason(val)
            ,_message(msg) {}
        ///compare
        bool operator==(const Reason &r) const {return _reason == r._reason;}
        ///retrieve reason code
        E code() const {return _reason;}
        ///retrieve attached message
        std::string_view message() const {return _message;}
        ///retrieve reason in string
        std::string_view get_reason_as_string() const {
            switch (_reason) {
                case no_reason: return "no reason given";
                case replace: return "replace";
                case not_found: return "not found";
                case position_limit: return "position limit";
                case max_leverage: return "max leverage";
                case unprocessed_fill:return "unprocessed fill";
                case invalid_params: return "invalid params";
                case incompatible_order: return "incompatible order";
                case invalid_amend: return "invalid amend";
                case unsupported: return "unsupported order type";
                case no_funds: return "no funds";
                case crossing: return "crossing";
                case exchange_error: return "exchange error";
                case internal_error: return "internal error";
                case low_liquidity: return "low liquidity";
                case exchange_overload: return "exchange overload";
                case too_small: return "too small amount";
                case unknown:
                default: return "unkown reason";
            }
        }

        friend std::ostream &operator<<(std::ostream &str, const Reason &res) {
            str << res.get_reason_as_string() << " " << res.message();
            return str;
        }

    protected:
        E _reason;
        std::string _message;

    };



    ///carries report of an order (excepct fill)
    struct Report {
        ///new state
        std::optional<State> new_state = {};
        ///error reason
        std::optional<Reason> reason = {};
        ///contains updated filled amount
        std::optional<Decimal> filled_amount = {};
        ///contains average fill price
        std::optional<Decimal> avg_price = {};
        ///order fills
        Fills fills = {};
    };

    enum class Behavior {
        ///standard behavior, reduce position, then open new position (no hedge)
        standard,
        ///increase position on given side (so we can have both buy and sell sides opened)
        hedge,
        ///reduce position, prevent to go other side (you need to SELL on long, BUY on short)
        reduce
    };

    enum class Origin {
        ///Unknown origin (there is no evidence who is responsible for creation of this order)
        unknown,
        ///Order has been created by this strategy instance
        strategy,
        ///Order has been restored from permanent storage
        restored,
        ///Order is liquidation order issued by exchange
        liquidation,
        ///Order is probably made manually by user intervention
        manual,
    };

    ///Order is undefined
    struct Undefined {};

    struct Options {
        ///specifies matching behavior
        Behavior behavior = Behavior::standard;
        ///specifies new position leverage - if applicable - default value: use shared leverage
        Decimal leverage = 0;
        /// specifies, that replace should use amend
        /**
         * Amend operation causes, that order's fill is perserved. If the order
         * is already done, the new order is canceled. If the order is
         * not in compatible state (for example order is discarded or associated),
         * the new order is discarded.
         *
         * If this argument is false, you can replace any order, however, if
         * the state of the replacing order is different than expected, the
         * replace operation can be rejected with reason Reason::unprocessed_fill
         *
         * @note this field is ignored for new order
         */
        bool amend = false;

        ///specified amount is in volume - so amount of money you want to spend on retrieve on trade (fees are not included)
        /** especially market orders can be executed as series of IOC orders if the
         * exchange doesn't support this feature
         */
        bool amount_is_volume = false;


        static constexpr Options Default() {return {};}
        friend std::ostream &operator<<(std::ostream &str, const Options &opt);
    };


    ///common part for all orders
    struct Common {
        Side side = {};
        Options options = {};
    };

    ///Market order
    struct Market: Common {
        Decimal amount;
        Market(Side s, Decimal amount, Options opt = Options::Default())
            :Common{s, opt},amount(amount) {}
        friend std::ostream &operator<<(std::ostream &str, const Options &opt);
    };

    ///Limit order
    struct Limit: Common {
        Decimal amount;
        Decimal limit_price;
        Limit(Side s, Decimal amount, Decimal limit_price, Options opt = Options::Default())
            :Common(s, opt),amount(amount), limit_price(limit_price) {}
        friend std::ostream &operator<<(std::ostream &str, const Options &opt);
    };

    ///Limit post only - rejlastected if would immediately match
    struct LimitPostOnly: Common {
        Decimal amount;
        Decimal limit_price;
        LimitPostOnly(Side s, Decimal amount, Decimal limit_price, Options opt = Options::Default())
            :Common(s, opt),amount(amount), limit_price(limit_price) {}
        friend std::ostream &operator<<(std::ostream &str, const Options &opt);
    };

    ///Limit immediate or cancel - rejects when order ends in orderbook (just fill up to limit)
    struct ImmediateOrCancel : Common {
        Decimal amount;
        Decimal limit_price;
        ImmediateOrCancel(Side s, Decimal amount, Decimal limit_price, Options opt = Options::Default())
            :Common(s, opt),amount(amount), limit_price(limit_price) {}
        friend std::ostream &operator<<(std::ostream &str, const Options &opt);

    };

    ///Stop order
    struct Stop: Common{
        Decimal amount;
        Decimal stop_price;
        Stop(Side s, Decimal amount, Decimal stop_price, Options opt = Options::Default())
            :Common(s, opt), amount(amount), stop_price(stop_price) {}
        friend std::ostream &operator<<(std::ostream &str, const Options &opt);
    };

    ///StopLimit order
    struct StopLimit: Common{
        Decimal amount;
        Decimal stop_price;
        Decimal limit_price;
        StopLimit(Side s, Decimal amount, Decimal stop_price, Decimal limit_price, Options opt = Options::Default())
            :Common(s, opt), amount(amount), stop_price(stop_price), limit_price(limit_price) {}
    };

    ///Trailing stop order
    struct TrailingStop: Common {
        Decimal amount;
        Decimal stop_distance;
        TrailingStop(Side s, Decimal amount, Decimal stop_distance, Options opt = Options::Default())
            :Common(s, opt), amount(amount), stop_distance(stop_distance) {}
        friend std::ostream &operator<<(std::ostream &str, const Options &opt);
    };

    ///Target and StopLoss order (OCO)
    struct TpSl: Common {
        Decimal amount;
        Decimal stop_price;
        Decimal limit_price;
        TpSl(Side s, Decimal amount, Decimal stop_price, Decimal limit_price, Options opt = Options::Default())
            :Common(s, opt), amount(amount), stop_price(stop_price), limit_price(limit_price) {}
        friend std::ostream &operator<<(std::ostream &str, const Options &opt);
    };
    ///Close position order (CFD)
    struct ClosePosition {
        std::string pos_id;
        Decimal remain;
        ClosePosition(const std::string  &pos, Decimal remain = 0):pos_id(pos), remain(remain) {}
        friend std::ostream &operator<<(std::ostream &str, const Options &opt);
    };

    ///Transfer money from one account to other account
    /**
     *  This order can be executed on any instrument (as instrument is ignored), however
     *  it is strongly recommended, to use instrument which is associated with the
     *  source account
     *
     *  The order doesn't generate fill. However upon successfull execution, it
     *  generates on_order, which is changed to filled
     *
     *  If transfer requires currency exchange, it is executed as spot market order on
     *  available pair. Such exchange is made only between known currency pairs, no
     *  transitional execution is performed
     */
    struct Transfer {
        Account target;
        Decimal amount;
        Transfer(const Account &target, Decimal amount)
            :target(target), amount(amount) {}
        friend std::ostream &operator<<(std::ostream &str, const Options &opt);
    };

    using Setup = std::variant<
                Undefined,
                Market,
                Limit,
                LimitPostOnly,
                Stop,
                StopLimit,
                TrailingStop,
                TpSl,
                ClosePosition,
                Transfer
            >;


    constexpr virtual ~IOrder() = default;

    ///get order state
    virtual State get_state() const = 0;

    ///get reason for current state
    virtual Reason get_reason() const = 0;

    ///get filled amount
    virtual Decimal get_filled() const = 0;

    ///get average execution price
    virtual Decimal get_avg_price() const = 0;

    ///retrieves replaced order (if order replacing other order)
    virtual std::shared_ptr<const IOrder> get_replaced_order() const = 0;
    ///retrieve associated instrument instance
    virtual Instrument get_instrument() const = 0;

    ///retrieve associated account instance
    virtual Account get_account() const = 0;

    ///retrieve order's initial setup
    virtual const Setup &get_setup() const = 0;

    ///retrieve binary representation
    virtual SerializedOrder to_binary() const = 0;

    virtual Origin get_origin() const = 0;

    ///Retrieve internal order id
    virtual std::string get_id() const = 0;

    ///Retrieve label
    virtual std::string get_label() const = 0;



    class Null;
};

#ifndef __CDT_PARSER__ //Eclipse CDT is able to break this section

template<typename T>
concept is_order = requires(T order) {
    {order.side}->std::convertible_to<Side>;
};



template<typename T>
concept order_has_amount = (is_order<T> && requires(T order) {
    {order.amount} -> std::convertible_to<Decimal>;
});


template<typename T>
concept order_has_options= (is_order<T> && requires(T order) {
    {order.options} ->std::convertible_to<IOrder::Options>;
});


#endif

class IOrder::Null: public IOrder {
public:
    virtual State get_state() const override {return State::undefined;}
    virtual Decimal get_avg_price() const override {return 0.0;}
    virtual Decimal get_filled() const override {return 0.0;}
    virtual Reason get_reason() const override {return Reason::no_reason;}
    virtual Instrument get_instrument() const override {return {};}
    virtual Account get_account() const override {return {};}
    virtual SerializedOrder to_binary() const override {return {};}
    virtual Origin get_origin() const override {return Origin::unknown;};
    virtual std::string get_id() const override {return {};}
    virtual std::string get_label() const override {return {};}
    virtual std::shared_ptr<const IOrder> get_replaced_order() const override {return {};}
    virtual const Setup &get_setup() const override {
        static Setup empty;
        return empty;
    }
};

class Order: public Wrapper<IOrder> {
public:

    using Wrapper<IOrder>::Wrapper;


    using State = IOrder::State;
    using Reason = IOrder::Reason;
    using Setup = IOrder::Setup;
    using Market = IOrder::Market;
    using Limit = IOrder::Limit;
    using LimitPostOnly = IOrder::LimitPostOnly;
    using Stop = IOrder::Stop;
    using StopLimit = IOrder::StopLimit;
    using TpSl = IOrder::TpSl;
    using TrailingStop = IOrder::TrailingStop;
    using ClosePosition = IOrder::ClosePosition;
    using ImmediateOrCancel = IOrder::ImmediateOrCancel;
    using Transfer = IOrder::Transfer;
    using Behavior = IOrder::Behavior;
    using Options = IOrder::Options;
    using Origin = IOrder::Origin;
    using Report = IOrder::Report;

    ///get order state
    State get_state() const {
        return _ptr->get_state();
    }

    ///get reason for current state
    Reason get_reason() const {
        return _ptr->get_reason();
    }

    ///get filled amount
    Decimal get_filled() const {
        return _ptr->get_filled();
    }

    static Decimal get_total(const Setup &setup) {
        return std::visit([](const auto &x) -> Decimal {
           if constexpr(order_has_amount<decltype(x)>) {
               return x.amount;
           } else {
               return 0;
           }
        }, setup);
    }

    ///get total amount
    Decimal get_total() const {
        return get_total(_ptr->get_setup());
    }

    ///remain amount to fill
    Decimal get_remain() const {
        return get_total() - get_filled();
    }

    ///order's initial setup
    const Setup &get_setup() const {
        return _ptr->get_setup();
    }

    ///get last executed price
    Decimal get_avg_price() const {
        return _ptr->get_avg_price();
    }

    ///associated instrument
    Instrument get_instrument() const {
        return _ptr->get_instrument();
    }

    ///associated instrument
    Account get_account() const {
        return _ptr->get_account();
    }

    static Side get_side(const Order::Setup &setup) {
        return std::visit([](const auto &x) -> Side {
            if constexpr(is_order<decltype(x)>) {
                return x.side;
            } else {
                return Side::undefined;
            }
        }, setup);
    }

    static const Options *get_options(const Order::Setup &setup) {
        return std::visit([](const auto &x) ->const Options * {
            if constexpr(order_has_options<decltype(x)>) {
                return &x.options;
            } else {
                return nullptr;
            }
        }, setup);

    }

    ///order's side
    Side get_side() const {
        return get_side(_ptr->get_setup());
    }

    ///returns true, if order is finished
    bool done() const {return IOrder::is_done(get_state());}
    ///returns true, if order is discard
    bool discarded() const {return get_state() == Order::State::discarded;}
    ///returns true, if order is rejected
    bool rejected() const {return get_state() == Order::State::rejected;}
    ///returns true, if order is canceled
    bool canceled() const {return get_state() == Order::State::canceled;}

    ///Retrieve serialized binary content of the order
    /**
     * @return a binary representation of the order in format specific to
     * service provider. The returned information can be used to
     * store order permanently (for example in database) and use this
     * information to restore the state of the order when connection
     * to the exchange is reestablished.
     *
     * The strategy retrieves state of all stored orders after init() through
     * on_order or on_fill
     */
    SerializedOrder to_binary() const {
        return _ptr->to_binary();
    }

    ///Retrieves order's origin (who created this order)
    /**
     * The strategy can receive orders that was not created by them. This
     * function returns information about who is responsible for creation
     * of this order.
     *
     * For example if the unexcpected order has origin `restored` the strategy
     * knows, that this order was created by previous instance, so it
     * can adapt its state and include this order to calculations
     *
     * @return order's origin
     */
    Origin get_origin() const {
        return _ptr->get_origin();
    }

    struct Hasher {
        auto operator()(const Order &ord) const {
            std::hash<std::shared_ptr<const IOrder> > hasher;
            return hasher(ord._ptr);
        }
    };


    ///Retrieve order's internal ID
    /**
     * @return a string containing order's internal ID. This ID can
     * be found on Fill as order_id. If you have Order instance, you
     * can filter fills by orders. However there is no reverse way
     * to retrieve Order from ID
     */
    std::string get_id() const {return _ptr->get_id();}

    ///Retrieve label which was set on place_order or replace_order
    std::string get_label() const {return _ptr->get_label();}

    ///Retrieve order which has been replaced
    /**
     * @return retrieves replaced order.
     *
     * @note referenced order must be active or must be stored. The reference
     * to the order is stored as weak reference. Once the replaced order
     * is no longer available, returns undefined order.
     */
    Order get_replaced_order() const {return Order(_ptr->get_replaced_order());}

};

inline std::string_view to_string(Order::State st) {
    switch(st) {
        case Order::State::active: return "active";
        case Order::State::associated: return "associated";
        case Order::State::canceled: return "canceled";
        case Order::State::discarded: return "discarded";
        case Order::State::filled: return "filled";
        case Order::State::rejected: return "rejected";
        case Order::State::restoring: return "restoring";
        case Order::State::sent: return "sent";
        case Order::State::undefined: return "undefined";
        case Order::State::waiting: return "waiting";
        default: return "unknown";
    }
}

inline std::string_view to_string(Order::Origin org) {
    switch(org) {
        case Order::Origin::liquidation: return "liquidation";
        case Order::Origin::manual: return "manual";
        case Order::Origin::restored: return "restored";
        case Order::Origin::strategy: return "strategy";
        default: return "unknown";
    }
}

inline std::string_view to_string(Order::Behavior b) {
    switch (b) {
        case Order::Behavior::hedge: return "hedge";
        case Order::Behavior::reduce: return "reduce";
        default: return "standard";
    }
}

inline std::ostream &operator<<(std::ostream &str, const IOrder::Options &x) {
    str << to_string(x.behavior);
    if (x.amend) str << ",amend";
    if (x.amount_is_volume) str << ",amount_is_volume";
    if (x.leverage>0) str << ",leverage=" << x.leverage;
    return str;
}
inline std::ostream &operator<<(std::ostream &str, const IOrder::Market &x) {
    str << to_string(x.side) << " " << x.amount << " MARKET (" << x.options << ")";
    return str;
}
inline std::ostream &operator<<(std::ostream &str, const IOrder::Limit &x) {
    str << to_string(x.side) << " " << x.amount << " LIMIT " << x.limit_price << " (" << x.options << ")";
    return str;
}
inline std::ostream &operator<<(std::ostream &str, const IOrder::LimitPostOnly &x) {
    str << to_string(x.side) << " " << x.amount << " LIMIT(post) " << x.limit_price << " (" << x.options << ")";
    return str;
}
inline std::ostream &operator<<(std::ostream &str, const IOrder::Stop &x) {
    str << to_string(x.side) << " " << x.amount << " STOP " << x.stop_price << " (" << x.options << ")";
    return str;
}
inline std::ostream &operator<<(std::ostream &str, const IOrder::StopLimit &x){
    str << to_string(x.side) << " " << x.amount << " STOP " << x.stop_price << " LIMIT " << x.limit_price << " (" << x.options << ")";
    return str;
}
inline std::ostream &operator<<(std::ostream &str, const IOrder::TrailingStop &x) {
    str << to_string(x.side) << " " << x.amount << " STOP trailing=" << x.stop_distance << " (" << x.options << ")";
    return str;
}
inline std::ostream &operator<<(std::ostream &str, const IOrder::TpSl &x) {
    str << to_string(x.side) << " " << x.amount << "OCO LIMIT " << x.limit_price << " STOP " << x.stop_price << " (" << x.options << ")";
    return str;
}
inline std::ostream &operator<<(std::ostream &str, const IOrder::ImmediateOrCancel &x) {
    str << to_string(x.side) << " " << x.amount << " LIMIT(ioc) " << x.limit_price << " (" << x.options << ")";
    return str;
}
inline std::ostream &operator<<(std::ostream &str, const IOrder::Transfer &x) {
    str << " TRANSFER "<< x.amount << " to=" << x.target.get_label();
    return str;
}
inline std::ostream &operator<<(std::ostream &str, const IOrder::ClosePosition &x) {
    str << "CLOSE " << x.pos_id << " (reduce_to=" << x.remain << ")";
    return str;
}

inline std::ostream &operator<<(std::ostream &str, const IOrder::Setup &x) {
    return std::visit([&](const auto &_x) -> std::ostream &{
        return str << _x;
    }, x);

}

}
