#pragma once

#include "types.hpp"
#include "defs.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <system_error>
#include "utils/decimal.hpp"
#include "utils/round.hpp"


namespace quarkbot {


enum class OrderType {
    ///alert - not order exactly, limit_price is mandatory, amount must be zero, is marked filled when price is reached from given side 
    /** alerts can be emulated if not supported on exchange */
    alert,
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

inline constexpr bool is_limit_order(OrderType type) {
    return type == OrderType::limit
        || type == OrderType::limit_post_only
        || type == OrderType::limit_ioc
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

struct OrderStatusUpdate {
    OrderStatus status;
    OrderRejectionReason rej_status = OrderRejectionReason::none;
    std::string string_param = {};        
};

///Initial order update - when order is pulled from exchange 
struct OrderInitialUpdate {
    std::string id = {};    
};

class Order {
public:


    struct State {
        ///original parameters -  adjusted
        const OrderParameters parameters = {};
        ///associated instrument
        PTradableInstrument instrument = {};
        ///order name
        std::string name = {};
        ///reference to replaced order
        std::weak_ptr<State> replaced_order = {};
        ///internal order ID
        std::string id = {};
        ///filled amount (calculated locally)
        Decimal filled = {};
        ///order status
        OrderStatus status = OrderStatus::sent; 
        ///if order rejected, there is reject reason
        OrderRejectionReason reject_reason = {};
        ///if order rejected, there is message if any
        std::string rejection_message = {};
        ///revision of last change
        /** @note connector must update order in strategy's thread */
        unsigned int rev = 0;
        ///revision of last seen change
        unsigned int seen_rev = 0;     
        ///queue of unprocessed fills
        std::queue<Fill> fills;
        ///awaiting coroutine
        awaitable<bool>::result awaiting = {};
        ///contains timestamp of last seen fill pulled from fills
        std::chrono::system_clock::time_point _last_seen_fill_time = {};
        ///contains index of last seen fill, if timestamp was same
        std::size_t _last_seen_fill_counter = 0;
        

        State(OrderParametersGen<Decimal> params, 
              PTradableInstrument instrument,
              std::string name,
              std::weak_ptr<State> replaced_order
        ):parameters(std::move(params))
         ,instrument(std::move(instrument))
         ,name(std::move(name))
         ,replaced_order(std::move(replaced_order)) {}
    };
        
    Order(std::shared_ptr<State> st):_state(std::move(st)) {}

    Order(OrderParametersGen<Decimal> params, 
        PTradableInstrument instrument,
        std::string name,
        Order replaced_order
    ):_state(std::make_shared<State>(std::move(params),std::move(instrument), std::move(name), replaced_order._state)) {}

    Order(OrderParametersGen<Decimal> params, 
        PTradableInstrument instrument,
        std::string name
    ):_state(std::make_shared<State>(std::move(params),std::move(instrument), std::move(name),  std::weak_ptr<State>{})) {}


    ///Wait for next event (awaitable)
    /**
    @retval true a state of order has been changed, or there are unprocessed fill
    @retval false order is done

    @note The function always returns immediatelly if there is unprocessed fill. It returns true, if there is posibility that
      state has been changed, and this information was note pulled out yet. Otherwise the function blocks, if co_awaited
      If the order is done, function immediatelly returns false
    */
    awaitable<bool> next_event() {
        if (!_state->fills.empty()) return true;
        if (_state->seen_rev != _state->rev) {_state->seen_rev = _state->rev; return true;}
        if (is_done_status(_state->status)) return false;
        return [st = _state](auto promise) {
            st->awaiting = std::move(promise);
        };
    }

    ///returns true if there is any unprocessed fill
    bool any_fill() const {
        return !_state->fills.empty();
    }

    ///reads next fill, 
    /**
    @return next unprocessed fill, or nullopt if none
    */
    std::optional<Fill> read_fill() {
        std::optional<Fill> out;
        if (any_fill()) {
            out.emplace(std::move(_state->fills.front()));
            _state->fills.pop();
            if (_state->_last_seen_fill_time  == out->time) _state->_last_seen_fill_counter++;
            else {
                _state->_last_seen_fill_time = out->time;
                _state->_last_seen_fill_counter = 0;
            }
        }
        return out;
    }

    

    ///get order parameters
    const OrderParametersGen<Decimal> &get_parameters() const {return _state->parameters;}
    ///get instrument
    PTradableInstrument get_instrument() const {return _state->instrument;}
    ///get order name
    const std::string &get_name() const {return _state->name;}    
    ///retrieve order instance which has been replaced
    /**
        @return optional containing order instance. Note that to return
            valid order instance, it must still exists somewhere in the
            system. Once the last reference is removed, the previous order is no longer
            available and function returns nullopt
    */
    std::optional<Order> get_replaced_order() const {
        auto lk = _state->replaced_order.lock();
        std::optional<Order> out;
        if (lk) out.emplace(lk);
        return out;
    }
    ///return internal id
    const std::string &get_id() const {return _state->id;}
    ///return filled amount
    Decimal get_filled() const {return _state->filled;}
    ///return get order status
    OrderStatus get_status() const {return _state->status;}
    ///get reason for rejection
    OrderRejectionReason get_reject_reason() const {return _state->reject_reason;}
    ///get rejection message
    const std::string &get_rejection_message() const {return _state->rejection_message;}

    

    ///update order status
    /**
    @note function is not MT safe. Ensure that it is called in strategy's thread
    */
    void update_order(OrderStatusUpdate &&update) {
        _state->status = update.status;
        _state->reject_reason = update.rej_status;
        if (!update.string_param.empty()) {
            if (update.rej_status == OrderRejectionReason::none) {
                _state->id = std::move(update.string_param);
            } else {
                _state->rejection_message = std::move(update.string_param);
            }
        }
         ++_state->rev;
        if (_state->awaiting) {
            _state->seen_rev = _state->rev;
            _state->awaiting(true);
        }
    }

    ///update order status
    /**
    @note function is not MT safe. Ensure that it is called in strategy's thread
    */
    void update_order(Fill &&fill) {
        _state->filled += fill.amount; 
        _state->fills.push(std::move(fill));

        if (_state->awaiting) {
            _state->awaiting(true);
        }
    }

    void update_order(OrderInitialUpdate &&st) {
        _state->status = OrderStatus::open;
        _state->id = st.id;
    }

    void cancel();
    Decimal get_turnover(Decimal price, Decimal filled = {}) const;
    bool done() const {return is_done_status(get_status());}

    bool operator==(const Order &) const = default;
    

protected:

    std::shared_ptr<State> _state = {};
};

using SerializedOrder = std::string;

}
    