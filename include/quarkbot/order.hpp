#pragma once

#include "basic_coro/concepts.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "defs.hpp"
#include "execution_worker.hpp"
#include "order_defs.hpp"
#include "abstract/iorder.hpp"
#include "utils/wrapper.hpp"
#include "strategy_fragment.hpp"
#include <cassert>
#include <chrono>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>



namespace quarkbot {

class TradableInstrument;
class Order;
struct OrderEvent;


class Order : public Wrapper<IOrder> {
public:

    using Wrapper<IOrder>::Wrapper;



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
  
        auto st = _ptr->receive_report(report);
        switch (st) {
            case IOrder::RcvStatus::already_pending: throw already_pending_exception();
            case IOrder::RcvStatus::done: return false;
            case IOrder::RcvStatus::updates: return true;
            default:break;
        };
        //register itself
        return [this, &report](awaitable<bool>::result promise) -> coro::prepared_coro {
            //perform registration
            auto st = _ptr->receive_report(report, promise);
            switch (st) {
                default:
                case IOrder::RcvStatus::already_pending: return promise(std::make_exception_ptr(already_pending_exception()));
                case IOrder::RcvStatus::updates: return promise(true);
                case IOrder::RcvStatus::done: return promise(false);
                case IOrder::RcvStatus::awaiting: return {};
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
        return _ptr->get_parameters();
    }
    ///get instrument
    TradableInstrument get_instrument() const;


    ///retrieve order instance which has been replaced
    /**
        @return optional containing order instance. Note that to return
            valid order instance, it must still exists somewhere in the
            system. Once the last reference is removed, the previous order is no longer
            available and function returns nullopt
    */
    std::optional<Order> get_replaced_order() const {
        std::optional<Order> out;
        auto lk = _ptr->get_replaced_order().lock();
        if (lk) out.emplace(lk);
        return out;
    }

    std::chrono::system_clock::time_point get_creation_time() const {
        return _ptr->get_creation_time();
    }
    void cancel() {
        _ptr->cancel();
    }

    ///Feed reports to callback
    /**
        @param cb callback. The function receives OrderReport &. The callback must return true to continue receiving, or false to
        stop receiving. When order is done, the callback is called once with final status and then callback is destroyed

        @return StrategyFragment allows to schedule execution of the coroutine

    */

    template<std::invocable<OrderReport &> _CB>
    StrategyFragment feed_to(_CB &&cb) {
        using _Res = std::invoke_result_t<_CB, OrderReport &>;
        static_assert(std::is_convertible_v<_Res, bool> || (coro::is_awaiter<_Res> && std::is_convertible_v<coro::awaiter_result<_Res>, bool>),
            "Callback must return bool or awaitable<bool> compatible");
        Order me(*this);
        auto coro = [](Order order, _CB cb) {
            OrderReport rpt;
            if (coro::is_awaiter<_Res>) {
                while (co_await order.receive(rpt) && bool(co_await cb(rpt)));
            } else {
                while (co_await order.receive(rpt) && bool(cb(rpt)));
            }
        };
        return coro(me, std::forward<_CB>(cb));
    }



protected:

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
    //allocate for report locally
    OrderEvent rpt;
    //initialize report
    rpt.order = Order(*this);

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
        ExecutionWorker::current().required().run(coro(Order(*this), std::move(hub)));
        //report ok waiting
        return true;
    }
}



}
    