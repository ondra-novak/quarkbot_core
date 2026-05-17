#pragma once

#include "ifc/types.hpp"
#include "utils/round.hpp"

namespace quarkbot {


enum class OrderType : char{
    ///alert - not order exactly, limit_price is mandatory, amount must be zero, is marked filled when price is reached from given side 
    /** alerts can be emulated if not supported on exchange */
    alert,
    ///market order - amount is mandarory
    market,
    ///limit order - amount and limit_price are mandatory
    limit,
    ///limit post only - order is reject when it would fill immediately - amount and limit_price are mandatory
    limit_post_only,
    ///stop order - amount and stop_price are mandatory
    stop,
    ///stop limit order - amount, stop_price and limit_price are mandatory
    stoplimit,
    ///pair of orders - one order is limit, second is stop. When one is filled, the other is canceled
    oco
};

enum class TimeInForce : char {
    ///Good until canceled
    gtc,
    ///Day only, canceled at close
    day,
    ///immediate or cancel
    ioc,
    ///at close
    atc,
    ///at open
    ato,    
    ///at crossing
    crossing    
};

inline constexpr bool is_limit_order(OrderType type) {
    return type == OrderType::limit
        || type == OrderType::limit_post_only
        || type == OrderType::oco
        || type == OrderType::alert;
}

inline constexpr bool is_stop_order(OrderType type) {
    return type == OrderType::stop
        || type == OrderType::stoplimit
        || type == OrderType::oco;
}

template<typename NumberType>
struct OrderParametersGen {
    ///order side
    Side side;
    ///order type
    OrderType type;
    ///amount (positive number)
    NumberType quantity; //mandatory
    ///limit price (for limit orders)
    NumberType limit_price = {};
    ///stop price (for stop orders)
    NumberType stop_price = {};
    ///max leverage (0 = disabled)
    double leverage = 0;
    
    ///reduce or close position
    bool reduce_only = false;
    ///create or increase to hedge side - can open reverse position if supported on exchange
    bool hedge = false;
    ///Enforce that stop or alert is triggered on local side, not on exchange, even if exchange supports it.    
    bool local_trigger = false;
    ///time in force
    TimeInForce time_in_force = TimeInForce::gtc;
    ///sets execution reason for given order 
    /**
      Allows to override execution reason for strategy orders. 
     */
    ExecutionReason reason_override = ExecutionReason::strategy_order;
};

using OrderParameters = OrderParametersGen<Decimal>;
using OrderRequest = OrderParametersGen<TargetValue>;



enum class OrderStatus : uint8_t {
    ///order sent, not confirmed yet
    sent,
    ///order is active, open, waiting in orderbook, waiting to trigger, there can be partial fills (see fills)
    open,    
    ///order is done, filled complete
    filled,
    ///order is done, has been canceled
    canceled,
    ///order is done, has been rejected
    rejected,    
    ///order is done, has been replaced by other order
    replaced,
    ///order has been recently restored from storage and exchange adapter is synchronizing its state
    restored,
    ///order is done, informations about the order are lost - this can happen as resolution of restored state. (order is no longer found in history)
    lost
};

enum class OrderRejectionReason : uint8_t{
    //no reason given
    none,
    //instrument not tradable
    not_tradable,
    //order is too large
    too_large,
    //order is too small
    too_small,    
    //no funds
    insufficient_funds,
    //invalid parameters
    invalid_params,
    //order cannot be replaced (incompatible settings)
    invalid_replace,
    //order cannot be replaced, because old order was not found (is probably done)
    order_not_found,
    //price is not allowed range
    price_range,
    //post only order would take liquidity
    post_only_taker,
    //minimum order volume
    min_volume,
    //exchange is overloaded
    overloaded,
    //exchange rate limit implemented
    rate_limited,
    //risk limit reached
    too_risky,
    //denied by exchange 
    permission_denied,
    //order configuration is not supported by this exchange
    unsupported,
    //order was not posted to exchange - connection stalled
    timeout,
    //order expired before delivered
    expired,
    //reduce order doesn't reduce position
    reduce_doesnt_reduce,
    //order rejected because too large slippage
    slippage,
    //rejected because exchange as an internal issue
    exchange_issue,
    //internal error  (probably adapter error, connection, etc)
    internal_error,
    //other reason (textural),
    other
};

inline constexpr bool is_done_status(OrderStatus status) {
    return status == OrderStatus::filled ||
           status == OrderStatus::canceled ||
           status == OrderStatus::rejected ||
           status == OrderStatus::replaced ||
           status == OrderStatus::lost ;
}

}