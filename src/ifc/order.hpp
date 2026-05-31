#pragma once

#include "basic_coro/prepared_coro.hpp"
#include "hub.hpp"
#include "ifc/abstract/orderdata.hpp"
#include "ifc/defs.hpp"
#include "ifc/order_defs.hpp"
#include "strategy_fragment.hpp"
#include "abstract/orderdata.hpp"
#include <cassert>
#include <chrono>
#include <optional>
#include <string_view>



namespace quarkbot {

class TradableInstrument;


class Order {
public:

    Order();
    Order(POrderAData data):_order_data(std::move(data)) {}

    ///return true, if order is valid
    /**
        return true order instance contains a valid order
        return false order instance was not initialized
     */
    explicit operator bool() const {return static_cast<bool>(_order_data);}

    ///return true, if order instance is valid, i.e. contains and order information
    /**
        return true order instance contains a valid order
        return false order instance was not initialized
     */
    bool valid() const {return static_cast<bool>(_order_data); }



    ///Receive order report
    /**
      Function is awaitable. The first it attempt to receive report in non-blocking mode. If no report is
      available yet, it can be co_await(ed) to wait for next report

      @param report reference to variable which receives a report. It is recommended to use the same variable to 
            receiver reports in cycle as the function can optimize waiting according on previous state. 
            At the beginning, status_changed variable is set to false. If the status has been changed
            this variable is set to true.
      @return awaitable returning bool
      @retval true event received
      @retval false order is done, no more events
      @exception await_canceled_exception - operation was canceled because there is already pending await
     */
    awaitable<bool> receive(OrderReport &report) {
        report.status_changed = false;
        if (!report.fills.empty()) return true;
        if (!_order_data) return false;
        if (_order_data->flush_updates(report, _order_data)) return true;
        return [this, &report](awaitable<bool>::result promise) -> coro::prepared_coro {
            auto st = _order_data->register_awaiter(report, _order_data, promise);
            switch (st) {
                default:
                case OrderAwaiting::rejected_pending: return promise(std::nullopt);
                case OrderAwaiting::rejected_updates: return promise(true);
                case OrderAwaiting::rejected_done: return promise(false);
                case OrderAwaiting::accepted: return {};
            }
        };
    }

    ///get order parameters
    const OrderParameters &get_parameters() const {
        return force_valid().get_parameters();
    }
    ///get instrument
    TradableInstrument get_instrument() const;

    ///get order name
    const std::string &get_name() const {
        return force_valid().get_name();
    }
    ///retrieve order instance which has been replaced
    /**
        @return optional containing order instance. Note that to return
            valid order instance, it must still exists somewhere in the
            system. Once the last reference is removed, the previous order is no longer
            available and function returns nullopt
    */
    std::optional<Order> get_replaced_order() const {
        std::optional<Order> out;
        auto lk = force_valid().get_replaced_order().lock();
        if (lk) out.emplace(lk);
        return out;
    }

    std::chrono::system_clock::time_point get_creation_time() const {
        return force_valid().get_create_time();
    }
    void cancel() {
        if (_order_data) _order_data->cancel();
    }

    
    bool operator==(const Order &) const = default;
    
    ///create hash (for unordered map)
    std::uint64_t get_hash() const {
        force_valid();
        std::hash<POrderAData> hasher;
        return hasher(_order_data);
    }


    ///Feed reports to opened hup
    /**
        Forwards all reports to given hub
        @param hub a hub which accepts OrderReport. 
        @return This is coroutine, it returns StartegyFragment, which should be discarded to start
             the coroutine. 
        @note The hub receives only reports, not order itself
    */    
    StrategyFragment feed_to(std::shared_ptr<Hub<OrderReport> > hub) {
        OrderReport rep;
        Order me (*this);
        while (co_await me.receive(rep) && co_await hub->send(std::move(rep)));
    }

    ///Feed orders and reports to opened hup
    /**
        Forwards all reports to given hub
        @param hub a hub which accepts pair<Order and  OrderReport>
        @return This is coroutine, it returns StartegyFragment, which should be discarded to start
             the coroutine. 
        @note the hub receives order instance as well. However the coroutine keeps itself 
            registered and receiving events. You can use order instance to cancel or replace order.
    */    
    StrategyFragment feed_to(std::shared_ptr<Hub<std::pair<Order, OrderReport> > > hub) {
        OrderReport rep;
        Order me (*this);
        while (co_await me.receive(rep) && co_await hub->send({me, std::move(rep)}));
    }

    ///Access internal state - reserved for adapters, the strategy should not use it
    auto get_handle() const {return _order_data;}

    ///Keep order alive when order instance is destroyed
    /**
    By default every order is canceled when its associated instance is destroyed. For example when
    your code stops referencing an open order, the cancel request is immediately sent to the exchange.
    This function deactivates this default behaviour, so keep alive order is kept alive on exchange even if its
    associated instance is destroyed
     */
    void set_keep_alive(bool k = true) {
        if (_order_data) _order_data->set_keep_alive(k);
    }

protected:
    POrderAData _order_data;

    ///Throws exeception if order is not valud
    const OrderInternalData &force_valid() const {
        if (!valid()) [[unlikely]] throw std::runtime_error("Working with order without a value [!valid()]");
        return *_order_data;
    }

};

#if 0



class Order {
public:

    Order() = default;
    Order(std::shared_ptr<OrderStrategyData> st):_state(std::move(st)) {
        assert(st);
        assert(st->adapter_data);
        assert(st->adapter_data->cancel);
        assert(st->adapter_data->instrument);        
    }


    ///Receive next event 
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
    awaitable<bool> receive() {
        //no state, no await
        if (!_state) return false;
        //any unprocessed fills? indicate we are ready
        if (!_state->fills.empty()) return true;
        //attempt to flush any updates - returns true, if flushed
        if (_state->adapter_data->flush_updates(*_state)) return true;
        //no fills, no updates, check status whether status is final, in this case, return false
        if (is_done_status(_state->status)) return false;
        //not fills, no updates, status is not final, we must wait for next evenr
        //prepare and return registration lambda called on co_await
        return [state = _state](auto promise) -> coro::prepared_coro{
            //register awaiter - returns false if there are updates
            if (state->adapter_data->add_awaiter(promise, *state)) return {};
            //there are updates, resolve promise with true
            return promise(true);
        };        
    }



    ///returns true if there is any unprocessed fill
    /** If you need also process fill, it is better to use read_fill() directly and test result optional */
    bool any_fill() const {
        return _state && !_state->fills.empty();
    }

    ///reads next fill, 
    /**
    @return next unprocessed fill, or nullopt if none
    */
    std::optional<Fill> read_fill() {
        std::optional<Fill> out;
        if (_state && !_state->fills.empty()) {
            out = _state->fills.front();
            _state->fills.pop_front();
        }
        return out;
    }

    ///return true, if order is valid
    /**
        return true order instance contains a valid order
        return false order instance was not initialized
     */
    explicit operator bool() const {return static_cast<bool>(_state);}

    ///return true, if order instance is valid, i.e. contains and order information
    /**
        return true order instance contains a valid order
        return false order instance was not initialized
     */
    bool valid() const {return static_cast<bool>(_state);    }

    void force_valid() const {
        if (!_state) [[unlikely]] throw std::runtime_error("Working with order without a value [!valid()]");
    }

    ///get order parameters
    const OrderParameters &get_parameters() const {
        if (!_state) [[unlikely]] return empty_order_parameters;
        return _state->adapter_data->parameters;
    }
    ///get instrument
    TradableInstrument get_instrument() const;

    ///get order name
    std::string_view get_name() const {return _state?_state->adapter_data->name:std::string_view();}    
    ///retrieve order instance which has been replaced
    /**
        @return optional containing order instance. Note that to return
            valid order instance, it must still exists somewhere in the
            system. Once the last reference is removed, the previous order is no longer
            available and function returns nullopt
    */
    std::optional<Order> get_replaced_order() const {
        std::optional<Order> out;
        if (!_state) [[unlikely]] return out;
        auto lk = _state->adapter_data->replaced_order.lock();
        if (lk) out.emplace(lk);
        return out;
    }
    ///return internal id
    std::string_view get_id() const {return _state?_state->id:std::string_view();}
    ///return filled amount
    Decimal get_filled() const {return _state?_state->filled:0;}
    ///return get order status
    OrderStatus get_status() const {
        if (_state) [[likely]] return _state->status;
        return OrderStatus::lost;
    }
    ///get reason for rejection
    OrderRejectionReason get_reject_reason() const {return _state?_state->reject_reason:OrderRejectionReason::none;}
    ///get rejection message
    std::string_view get_rejection_message() const {return _state?_state->rejection_message:std::string_view();}    
    ///returns time when order has been created
    std::chrono::system_clock::time_point get_create_time() const {return _state?_state->adapter_data->create_time:std::chrono::system_clock::time_point();}

    ///cancel order
    /** @note if order has done status, operation is no-op */
    void cancel() {
        force_valid();
        _state->cancel();
    }
    ///Keep order alive even if instance is destroyed
    /**
        By default, when order instance is destroyed (last reference is released), the cancel is automatically sent.
        Call this function to keep order alive after instance destruction
    */
    void keep_alive(bool k = true) {
        force_valid();
        _state->keep_alive = k;
    }

    Decimal get_turnover() const {
        return _state?_state->turnover:0;
    }

    Decimal get_vwap() {
        if (_state && _state->filled) [[likely]]  return _state->turnover/_state->filled;
        else return 0;
    }
    

    bool is_done() const {return is_done_status(get_status());}

    
    bool operator==(const Order &) const = default;
    
    ///create hash (for unordered map)
    std::uint64_t get_hash() const {
        force_valid();
        std::hash<std::shared_ptr<OrderStrategyData> > hasher;
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
        force_valid();
        Order ord = *this;
        while (co_await(ord.next()) && co_await hub->send(ord));
    }

    ///Access internal state - reserved for adapters, the strategy should not use it
    auto get_handle() const {return _state;}

protected:

    std::shared_ptr<OrderStrategyData> _state = {};
};

#endif

}
    