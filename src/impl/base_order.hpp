#pragma once
#include <basic_coro/prepared_coro.hpp>
#include "basic_coro/awaitable.hpp"
#include "ifc/order.hpp"
#include "ifc/defs.hpp"
#include "utils/spin_mutex.hpp"
#include "ifc/execution_worker.hpp"
#include <memory>
#include <deque>
#include <optional>
#include <variant>


namespace quarkbot {


class BaseOrder : public IOrder {
public:
    virtual ~BaseOrder() = default;

    struct Rejection {
        OrderRejectionReason reason;
        std::string text = {};
    };

    BaseOrder(const OrderParameters &params,
             PTradableInstrument instrument,
             POrder replaced,
             std::string_view name);

    using Event = std::variant<std::monostate, OrderStatus, Fill, Rejection>;
    using EventQueue = std::deque<Event>;

   

    void post_update(OrderStatus status) ;

    void post_update(Fill fill);

    void post_update(Rejection rej);

    void reject(OrderRejectionReason rsn) {
        post_update(Rejection{rsn});
    }
    void reject(OrderRejectionReason rsn, std::string text) {
        post_update(Rejection{rsn, std::move(text)});
    }

    virtual coro::awaitable<bool> wait_event() override;

    virtual Decimal get_filled_amount() const override {return _filled_amount;}
    virtual bool any_fill() const override;
    virtual std::optional<Fill> read_fill() override;
    virtual const OrderParameters &get_parameters() const override;
    virtual OrderStatus get_status() const override;
    virtual bool is_done() const override;
    virtual PTradableInstrument get_instrument() const override;    
    virtual std::string_view get_name() const override ;
    virtual POrder get_replaced_order() const override ;
    virtual OrderRejectionReason get_rejection_reason() const override {return _rejection_reason;}
    virtual std::string_view get_rejection_message() const override {return _rejection_message;}


protected:

    OrderParameters _params;
    PTradableInstrument _instrument;
    std::weak_ptr<IOrder> _replaced;
    std::string _name;
    std::string _rejection_message;
    Decimal _filled_amount = {};
    OrderStatus _status = OrderStatus::sent;
    OrderRejectionReason _rejection_reason = OrderRejectionReason::none;
    mutable spin_mutex _mx;
    EventQueue _events = {};
    ProxyResult<bool> _event_waiter;

    void flush_statuses();

};

}