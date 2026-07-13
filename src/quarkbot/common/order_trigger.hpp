#pragma once 

#include "quarkbot/abstract/orderdata.hpp"
#include "quarkbot/defs.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/order.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/event_stream.hpp"
#include <memory>
namespace quarkbot {

///Object intended to be used by IExchange adapter to support local triggered orders
class OrderTrigger: public std::enable_shared_from_this<OrderTrigger> {
public:
    using Stream = EventStream<Trade>;
    static std::shared_ptr<OrderTrigger> create(ExecutionWorker worker) {
        return std::make_shared<OrderTrigger>(std::move(worker));
    }

    OrderTrigger(ExecutionWorker worker):_worker(std::move(worker)) {}


    ///place triggered order with special place request
    std::shared_ptr<OrderInternalData> place_order(PTradableInstrument instrument, 
                    const OrderParameters &trig_params, //params reported by order while trigger phase                    
                    std::shared_ptr<OrderInternalData> order_to_replace, 
                    std::function<Order()> place_request);



    ///place ordinary localy triggered order
    std::shared_ptr<OrderInternalData> place_order(PTradableInstrument instrument, 
                const OrderParameters &trig_params,
                std::shared_ptr<OrderInternalData> order_to_replace);

    bool convert_params_to_request(const OrderParameters &params, OrderRequest &request);


protected:
    ExecutionWorker _worker;


};
}