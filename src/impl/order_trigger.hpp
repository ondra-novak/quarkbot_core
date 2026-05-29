#pragma once 

#include "ifc/abstract/orderdata.hpp"
#include "ifc/defs.hpp"
#include "ifc/order.hpp"
#include "ifc/order_defs.hpp"
#include "ifc/strategy_fragment.hpp"
#include "ifc/stream/trade.hpp"
#include "ifc/streaming.hpp"
#include "utils/hashable.hpp"
#include <memory>
#include <unordered_map>
namespace quarkbot {

///Object intended to be used by IExchange adapter to support local triggered orders
class OrderTrigger: public std::enable_shared_from_this<OrderTrigger> {
public:
    
    static std::shared_ptr<OrderTrigger> create(ExecutionWorker worker);

    ///don't use - to create object, call create()
    OrderTrigger(ExecutionWorker worker);

    OrderTrigger(const OrderTrigger &) = delete;
    OrderTrigger &operator=(const OrderTrigger &) = delete;

    Order place_order(PTradableInstrument instrument, 
                    const OrderParameters &params, 
                    std::shared_ptr<OrderStrategyData> order_to_replace, 
                    std::string_view name, std::size_t param_class_hash);

    bool cancel_order(POrderAData ord);
    

protected:

    using Stream = EventStream<Trade>;

    struct State {
        Stream *stream = nullptr;
        bool canceled = false;
    };

    using OrderMap = std::unordered_map<POrderAData, State, Hasher<POrderAData>>;

    std::mutex _mx;
    OrderMap _order_map;
    ExecutionWorker _worker;

    static StrategyFragment monitor_order(std::shared_ptr<OrderTrigger> me, POrderAData order);

    template<typename Fn>
    auto update_state(const POrderAData &ord, Fn &&fn);
    void unreg_order(const POrderAData &ord);

};

}