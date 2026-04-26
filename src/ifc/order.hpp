#pragma once

#include "basic_coro/coro_frame.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "ifc/execution_worker.hpp"
#include "ifc/stream_defs.hpp"
#include "types.hpp"
#include "defs.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <variant>
#include "utils/decimal.hpp"
#include "utils/double_buffer.hpp"
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

    using Update = std::variant<Fill, OrderStatusUpdate>;

    struct State{
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

        std::mutex mx;

        DoubleBufferQueue<Update> updates;

        ResultAndExecWorker<bool> awaiting = {};
        

        State(OrderParametersGen<Decimal> params, 
              PTradableInstrument instrument,
              std::string name,
              std::weak_ptr<State> replaced_order
        ):parameters(std::move(params))
         ,instrument(std::move(instrument))
         ,name(std::move(name))
         ,replaced_order(std::move(replaced_order))
         ,updates(mx)
         {}


        void flush_state() {
            while (!updates.empty() && std::holds_alternative<OrderStatusUpdate>(updates.front())) {
                auto st = std::get<OrderStatusUpdate>(updates.front());
                status = st.status;
                if (status == OrderStatus::open) {
                    id = std::move(st.string_param);
                } else if (is_done_status(status)) {
                    reject_reason = st.rej_status;
                    rejection_message = std::move(st.string_param);
                }
                updates.pop();
            }
        }
        void update(Update &&u) {
            updates.push(std::move(u));
            if (awaiting) {
                awaiting(true);
            }
        }

        std::optional<bool> pull() {
            {
                std::scoped_lock _(mx);
                if (updates.empty()) {
                    if (is_done_status(status)) return false;
                    return {};
                }
            }
            flush_state();
            return true;
        }
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
    @retval true a state of order has been changed, or there are unprocessed fill. State update is applied to order instance.
    @retval false order is done
    @note If co_awaited, the function will suspend until there is new event (fill or status update) for this order. 
    @note only one co_await can be active at the same time, otherwise behavior is undefined. 
           You must co_await from execution thread, otherwise exception is thrown. However it is 
           possible to co_await in different execution thread than order has been created.
    */
    awaitable<bool> next_event() {
        auto &st = *_state;
        auto r = st.pull();
        if (r.has_value()) return *r;
        return [state = _state](auto promise) {
            auto r = state->pull();
            if (r.has_value()) return promise(r.value());
            state->awaiting = std::move(promise);
            return coro::prepared_coro{};
        };
    }

    ///returns true if there is any unprocessed fill
    bool any_fill() const {
        return !_state->updates.empty() && std::holds_alternative<Fill>(_state->updates.front());
    }

    ///reads next fill, 
    /**
    @return next unprocessed fill, or nullopt if none
    */
    std::optional<Fill> read_fill() {
        std::optional<Fill> out;
        if (any_fill()) {
            out.emplace(std::move(std::get<Fill>(_state->updates.front())));
            _state->updates.pop();
            _state->flush_state();
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
    */
    void update_order(Update &&update) {
        _state->update(std::move(update));
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
    