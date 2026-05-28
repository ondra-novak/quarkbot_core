#pragma once

#include "hub.hpp"
#include "strategy_fragment.hpp"
#include "abstract/order_internal.hpp"
#include <chrono>



namespace quarkbot {

class TradableInstrument;
    
class Order {
public:

    Order(std::shared_ptr<OrderInternalState> st):_state(std::move(st)) {}


    ///Wait for next event (awaitable)
    /**
    @retval true a state of order has been changed, or there are unprocessed fill. State update is applied to order instance.
    @retval false order is done
    @note If co_awaited, the function will suspend until there is new event (fill or status update) for this order. 
    @note only one co_await can be active at the same time, otherwise behavior is undefined. 
           You must co_await from execution thread, otherwise exception is thrown. However it is 
           possible to co_await in different execution thread than order has been created.

    @note new order state is populated only during this operation. Even if you know, that new state of order is avaialble, you 
    still need to call this function to correctly update internals of the order

        
    */
    awaitable<bool> next() {
        return OrderInternalState::next_event(_state);
    }



    ///returns true if there is any unprocessed fill
    /** If you need also process fill, it is better to use read_fill() directly and test result optional */
    bool any_fill() const {
        return !_state->fills.empty();
    }

    ///reads next fill, 
    /**
    @return next unprocessed fill, or nullopt if none
    */
    std::optional<Fill> read_fill() {
        std::optional<Fill> out;
        if (!_state->fills.empty()) {
            out = _state->fills.front();
            _state->fills.pop_front();
        }
        return out;
    }

    auto &get_all_fills() {return _state->fills;}
    auto &get_all_fills() const {return _state->fills;}

    ///get order parameters
    const OrderParametersGen<Decimal> &get_parameters() const {return _state->parameters;}
    ///get instrument
    TradableInstrument get_instrument() const;
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
    ///returns time when order has been created
    std::chrono::system_clock::time_point get_create_time() const {return _state->create_time;}

    ///update order status
    /**
    */
    void update_order(OrderInternalState::Update &&update) {
        _state->update(std::move(update));
    }


    void cancel();
    Decimal get_turnover(Decimal price, Decimal filled = {}) const;
    bool done() const {return is_done_status(get_status());}

    
    bool operator==(const Order &) const = default;
    
    ///create hash (for unordered map)
    std::uint64_t get_hash() const {
        std::hash<std::shared_ptr<OrderInternalState> > hasher;
        return hasher(_state);
    }

    ///Feed events to opened hup
    /**
        Forwards all events to hub. The hub must accept Order instance. Once all events are
        forwarded, you should avoid to call next() on the Order. You can also install only
        one feeder to an order.

        Orders pushed to hub are already prepared for data and fills retrieval, do not call next() again
    */    
    StrategyFragment feed_to(std::shared_ptr<Hub<Order> > hub) {
        Order ord = *this;
        while (co_await(ord.next()) && co_await hub->send(ord));
    }


    ///Retrieve internal record key - for some advanced persistence
    auto get_record_key() const {return _state->key;}

    ///Access internal state - reserved for adapters, the strategy should not use it
    auto get_handle() const {return _state;}

protected:

    std::shared_ptr<OrderInternalState> _state = {};
};



}
    