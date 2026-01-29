#pragma once

#include "types.hpp"
#include "defs.hpp"
#include <cstdint>
#include <optional>
#include "utils/round.hpp"

namespace quarkbot {


enum class OrderType {
    ///market order - amount is mandarory
    market,
    ///limit order - amount and limit_price are mandatory
    limit,
    ///limit post only - order is reject when it would fill immediately - amount and limit_price are mandatory
    limit_post_only,
    ///immediate or cancel - amount and limit_price are mandatory
    limit_ioc,
    ///stop order - amount and stop_price are mandatory
    stop,
    ///stop limit order - amount, stop_price and limit_price are mandatory
    stoplimit,
    ///pair of orders - one order is limit, second is stop. When one is filled, the other is canceled
    oco
};

template<typename NumberType>
struct OrderParametersGen {
    ///order side
    Side side;
    ///order type
    OrderType type;
    ///amount (positive number)
    NumberType amount; //mandatory
    ///limit price (for limit orders)
    NumberType limit_price = {};
    ///stop price (for stop orders)
    NumberType stop_price = {};
    ///trailing offset - round strategy is applied to final price
    NumberType trailing_offset = {};
    ///max leverage (0 = disabled)
    double leverage = 0;
    
    ///reduce or close position
    bool reduce_only = false;
    ///create or increase to hedge side - can open reverse position if supported on exchange
    bool hedge = false;
};

using OrderParameters = OrderParametersGen<Fixed>;
using OrderRequest = OrderParametersGen<Rounded>;

enum class OrderStatus : uint8_t {
    ///order sent, not confirmed yet
    sent,
    ///order is active, open, waiting in orderbook
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
    restored
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
    //internal error  (probably adapter error, connection, etc)
    internal_error,
    //other reason (textural),
    other
};

inline constexpr bool is_done_status(OrderStatus status) {
    return status == OrderStatus::filled ||
           status == OrderStatus::canceled ||
           status == OrderStatus::rejected ||
           status == OrderStatus::replaced;
}


class IOrder {
public:
    virtual ~IOrder() = default;

    virtual const OrderParameters &get_parameters() const = 0;
    ///Returns order status
    /**
        @return OrderStatus enumeration

        @note the status is not updated asynchronously. You need to call  wait_event() or read_fill() to properly
        update status. This should be done in strategy's thread
    */
    virtual OrderStatus get_status() const = 0;

    ///Retrieve order's rejection reason. It can appear only when reject status
    virtual OrderRejectionReason get_rejection_reason() const = 0;
    ///Get rejection message
    virtual std::string_view get_rejection_message() const = 0;
    
    ///Returns true, if order is done

    virtual bool is_done() const = 0;
    ///Retrieve next fill
    /**
    The function removes unprocessed fill from the queue and returns it. If there
    is no fill, returns nullopt. You will not receive fills twice, so you need to store fill somewhere
     */
    virtual std::optional<Fill> read_fill() = 0;
    ///Determine if there is an equeued fill
    /**
        The function doesn't change status or anything in the queue. It is better to call read_fill() and check
        for result.
    */
    virtual bool any_fill() const = 0;
    ///wait for event
    /**
      @retval true event happened
      @retval false eof, no more events

      @note the function returns immediately true if there is an unprocessed fill. Cycling without 
      @note the final status is updated, once all fills are processed
      @note event market order can be in `open` state if there are unprocessed fills      

      @code
      while (co_await order.wait_event()) {
          auto f = read_fill();
          if (f) process_fill(*f);
      }
      @endcode
     */
    virtual awaitable<bool> wait_event() = 0;
    ///Get associated tradable instrument
    virtual PTradableInstrument get_instrument() const = 0;
    ///Get order name (assigned by strategy)
    virtual std::string_view get_name() const = 0;
    virtual Fixed get_filled_amount() const = 0;

    ///Retrieves order which has been replaced by this order
    /**
    @return replaced order
    @note Function will return nullptr if there is no replaced order. Also note that replaced order
    is kept as long as exists somewhere in the system. So if you need it, you should hold it somewhere else,
    otherwise it is destroyed (internally weak_ref)
    */
    virtual POrder get_replaced_order() const = 0;

    ///cancel current order
    /**
    if order is already in final state, the function does nothing.
    The cancel operation is confirmed by sending order to final state.
    @note the cancel doesn't set to canceled state, it is set when exchange confirms cancel. If order
    is filled during waiting for confirmation then final state is filled.
    */
    virtual void cancel() = 0;


    ///Await for final order status
    /**
    @param callback function is called for every fill
    @return (asynchronous) returns final status of the order
     */
    template<std::invocable<Fill> Fn>
    awaitable<OrderStatus> run(Fn callback) {
        while (co_await this->wait_event()) {
            auto f = this->read_fill();
            if (f) callback(*f);            
        }
        co_return get_status();
    }

};



}
    