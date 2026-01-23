#include "../ifc/order.hpp"
#include "coro/src/basic_coro/prepared_coro.hpp"
#include "ifc/defs.hpp"
#include "utils/coro_dispatch.hpp"
#include "utils/spin_mutex.hpp"
#include <memory>
#include <mutex>
#include <deque>
#include <optional>
#include <variant>


namespace quarkbot {


class BaseOrder : public IOrder {
public:
    virtual ~BaseOrder() = default;

    BaseOrder(const OrderParameters &params,
             PTradableInstrument instrument,
             POrder replaced,
             std::string_view name);

    using Event = std::variant<std::monostate, OrderStatus, OrderFill>;
    using EventQueue = std::deque<Event>;


    coro::prepared_coro post_update(OrderStatus status) ;

    coro::prepared_coro post_update(OrderFill fill);
    virtual coro::awaitable<bool> wait_event() override;

    virtual double get_remaining_amount() const override;
    virtual bool any_fill() const override;
    virtual std::optional<OrderFill> read_fill() override;
    virtual OrderParameters get_parameters() const override;
    virtual OrderStatus get_status() const override;
    virtual bool is_done() const override;
    virtual PTradableInstrument get_instrument() const override;    
    virtual std::string_view get_name() const override ;
    virtual POrder get_replaced_order() const override ;


protected:

    OrderParameters _params;
    PTradableInstrument _instrument;
    std::weak_ptr<IOrder> _replaced;
    std::string _name;
    double _filled_amount = 0.0;
    OrderStatus _status = OrderStatus::sent;
    mutable spin_mutex _mx;
    EventQueue _events = {};
    CoroDispatchProxy<bool> _event_waiter = {};

    void flush_statuses();

};

}