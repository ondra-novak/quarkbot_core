#pragma once

#include <trading_api/exchange.h>

#include <optional>
namespace trading_api {

class SimInstrument;

namespace simulator {

///Market matching engine (for simulation)
class Matching {
public:

    ///Contains information about execution
    struct Execution {
        ///associated order
        Order order = {};
        ///executed side
        Side side = Side::undefined;
        ///executed at given price
        Decimal price = {};
        ///executed price
        Decimal size = {};
    };

    struct Spread {
        Decimal bid = Decimal::nan();
        Decimal bid_size = Decimal::inf();
        Decimal ask = Decimal::nan();
        Decimal ask_size= Decimal::inf();
    };

    ///sets new spread
    /**
     *
     * @note no execution is made, you need to call get_executions()
     */
    void set_spread(const Spread &spread);
    ///sets new trade
    /**
     * Sets trade happened in simulated market (data can be read from a replay file)
     * This trade is matched agains current limit orders.
     * @param price price of the trade
     * @param size size of the trade. The size is consumed by limit orders and if total
     * matched size is larger than this field, it can create partial fills
     *
     * @note no execution is made, you need to call get_executions()
     */
    void set_trade(Decimal price, Decimal size);


    ///Retrieve current spread
    /**
     * the retrieved spread also includes active limit orders
     */
    Spread get_spread() const;

    ///Limit order
    /**
     * @note limit orders are canceled in case of partial fill. The
     * simulator controller must place new order if it needs to continue in filling
     */
    struct Limit {
        ///associated Order instance
        Order order;
        ///side
        Side side;
        ///amount to execute
        Decimal amount;
        ///limit price
        Decimal limit_price;
    };

    ///Stop order
    struct Stop {
        ///associated Order instance
        Order order;
        ///side
        Side side;
        ///amount to execute
        Decimal amount;
        ///stop price
        Decimal stop_price;
    };

    ///StopLimit order
    /**
     * @note this changes to Limit order once it is triggered.
     * The simulator controller must not place StopLimit order, if it
     * is partially filled
     */
    struct StopLimit {
        ///associated Order instance
        Order order;
        ///side
        Side side;
        ///amount
        Decimal amount;
        ///stop price
        Decimal stop_price;
        ///limit price once order is activated
        Decimal limit_price;
    };

    ///Trailing stop order
    struct TrailingStop {
        ///associated Order instance
        Order order;
        ///side
        Side side;
        ///amount
        Decimal amount;
        ///distance of the stop order
        Decimal distance;
        ///this field is used to track current stop place, can be set to nan
        Decimal stop_price = Decimal::nan();
    };

    ///OCO order TP/SL
    struct TpSl {
        ///associated Order instance
        Order order;
        ///side
        Side side;
        ///amount
        Decimal amount;
        ///stop price
        Decimal stop_price;
        ///limit price
        Decimal limit_price;
    };

    using WaitingOrder = std::variant<Limit, Stop, StopLimit, TrailingStop, TpSl>;

    ///Place market order
    /**
     * Market order is immediatelly executed. It is executed whole. This is why
     * function returns Execution report
     *
     * @param ord associated order
     * @param side side
     * @param amount amount
     * @return execution report
     */
    Execution place_market_order(Order ord, Side side, Decimal amount);
    ///Place waiting order
    /**
     * @param ord order to place
     * @note order is not executed. You need to call get_executions()
     */
    void place_waiting_order(WaitingOrder ord);
    ///Cancels order
    /**
     * @param ord associated order instance
     * @retval true canceled
     * @retval false not found
     */
    bool cancel_order(Order ord);

    ///Simulates execution for current state (spread, trade)
    /**
     * @return list of executions
     * @note all executed orders are canceled (even if partial executions). Conditional
     * order can be modified (can change its state). Trailing stop orders
     * can be moved depend on new price
     */
    std::vector<Execution> get_executions();
    ///Calculates price for conversion ratio etc.
    Decimal get_effective_price() const;

protected:
    std::vector<WaitingOrder> _orders;
    std::vector<WaitingOrder> _updates;
    Spread _spread = {};
    Decimal _last = 0_dec;
    Decimal _last_size = 0_dec;
    void update_spread();

};


}


}

