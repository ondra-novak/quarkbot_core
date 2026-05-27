#pragma once 

#include "ifc/defs.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/order.hpp"
#include "ifc/strategy_fragment.hpp"
#include "ifc/streaming.hpp"
#include "utils/hashable.hpp"
#include <memory>
#include <unordered_map>
namespace quarkbot {

///Object intended to be used by IExchange adapter to support local triggered orders
class OrderTrigger: public std::enable_shared_from_this<OrderTrigger> {
public:
    
    static std::shared_ptr<OrderTrigger> create(PExecutionWorker worker);

    ///don't use - to create object, call create()
    OrderTrigger(PExecutionWorker worker);

    OrderTrigger(const OrderTrigger &) = delete;
    OrderTrigger &operator=(const OrderTrigger &) = delete;

    ///register the order for local triggering
    /**
        @param ord Order instance, it should be already validated and prepared to be placed. If this is replace, the order must carry a replaced order

        @note The function doesn't check for local_trigger flag, so it can be called to emulate alert and stop orders when exchange doesn't support such type

        Errors are emited as order rejections
    */
    void register_order(Order ord);
    ///request to cancel order
    void cancel_order(Order ord);

    

protected:

    using Stream = EventStream<Trade>;

    struct State {
        std::variant<std::monostate, Stream *, Order *> phase;
        bool canceled = false;
    };

    using OrderMap = std::unordered_map<Order, State, Hasher<Order>>;

    std::mutex _mx;
    OrderMap _order_map;
    PExecutionWorker _worker;

    static StrategyFragment monitor_order(std::shared_ptr<OrderTrigger> me, Order order);

    bool cancel_order_for_replace(const Order &ord);
    template<typename Fn>
    auto update_state(const Order &ord, Fn &&fn);
    void unreg_order(const Order &ord);

};

}