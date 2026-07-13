#pragma once

#include "basic_coro/prepared_coro.hpp"
#include "abstract/orderdata.hpp"
#include "defs.hpp"
#include "execution_worker.hpp"
#include "order_defs.hpp"
#include "strategy_fragment.hpp"
#include "abstract/orderdata.hpp"
#include <cassert>
#include <chrono>
#include <exception>
#include <optional>
#include <stdexcept>



namespace quarkbot {

class TradableInstrument;
class Order;
struct OrderEvent;


class Order {
public:

    Order()=default;
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
        //invalid order instance - false
        if (!_order_data) return false;

        auto st = _order_data->receive_report(report);
        switch (st) {
            case OrderReceiveReportStatus::already_pending: throw already_pending_exception();
            case OrderReceiveReportStatus::done: return false;
            case OrderReceiveReportStatus::updates: return true;
            default:break;
        };
        //register itself
        return [this, &report](awaitable<bool>::result promise) -> coro::prepared_coro {
            //perform registration
            auto st = _order_data->receive_report(report, promise);
            switch (st) {
                default:
                case OrderReceiveReportStatus::already_pending: return promise(std::make_exception_ptr(already_pending_exception()));
                case OrderReceiveReportStatus::updates: return promise(true);
                case OrderReceiveReportStatus::done: return promise(false);
                case OrderReceiveReportStatus::awaiting: return {};
            }
        };
    }

    ///Forward next order event to hub device
    /**
    @param hub Hub device.
    @note OrderEvent is sent to the hub device, which contains OrderReport and Order itself
    @note Operation is one shot. Once the report is received, you need to call this function again to retrieve new report
    @retval true scheduled
    @retval false order is done, no more reports are expected
     */
    template<HubProducer<OrderEvent> _Hub>
    bool receive_at(_Hub hub);

    ///get order parameters
    const OrderParameters &get_parameters() const {
        return force_valid().get_parameters();
    }
    ///get instrument
    TradableInstrument get_instrument() const;

    ///get order id
    /**
        Because id is not part of report, it can be filled anytime regardless on whether report has been received
    */
    const std::string &get_id() const {
        return force_valid().get_id_unsafe();
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


    ///Access internal state - reserved for adapters, the strategy should not use it
    auto get_handle() const {return _order_data;}



protected:
    POrderAData _order_data;

    ///Throws exeception if order is not valud
    const OrderInternalData &force_valid() const {
        if (!valid()) [[unlikely]] throw std::runtime_error("Working with order without a value [!valid()]");
        return *_order_data;
    }

    static std::runtime_error already_pending_exception() {
        return std::runtime_error("Order is already awaited for next event. You cannot await the same order simultaneously from multiple coroutines");
    }

    
};

///A structure  carrying latest order report and associated order.
struct OrderEvent : OrderReport{
    Order order;
};

template<HubProducer<OrderEvent> _Hub>
bool Order::receive_at(_Hub hub) {
    //ensure that order is valid
    force_valid();
    //allocate for report locally
    OrderEvent rpt;
    //initialize report
    rpt.order = Order(_order_data);

    //attempt to receive report non-blocking way - if done, then this don't need awaiting
    awaitable<bool> awt = receive(rpt);

    //we have a result!
    if (awt.await_ready()) {
        //retrieve result
        bool resp = awt.await_resume();
        //if result is false, order is done, report it
        if (!resp) return false;

        //order is not done, so we have report
        auto coro = [](OrderEvent ev, _Hub hub) -> StrategyFragment {
            co_await hub.send(std::move(ev));
        };
        //run coroutine responsible to pass the report to the hub
        ExecutionWorker::current().required().run(coro(std::move(rpt), std::move(hub)));
        //report ok waiting
        return true;
    }  else {
        //report was not received, so start coroutine to receive and deliver report

        auto coro = [](Order ord, _Hub hub) -> StrategyFragment {
            OrderEvent ev;
            ev.order = std::move(ord);
            co_await ev.order.receive(ev);
            co_await hub.send(std::move(ev));
        };
        //start coroutine, pass order and hub
        ExecutionWorker::current().required().run(coro(Order(_order_data), std::move(hub)));
        //report ok waiting
        return true;
    }
}


}
    