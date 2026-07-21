#pragma once 

#include "../common/orderdata.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/order.hpp"
#include "quarkbot/stream/trade.hpp"
#include <memory>
namespace quarkbot {

///Object intended to be used by IExchange adapter to support local triggered orders
class OrderTrigger {
public:
    using Stream = EventStream<Trade>;


    ///place triggered order with special place request
    static POrder place_order(PTradableInstrument instrument, 
                    const OrderParameters &trig_params, //params reported by order while trigger phase                    
                    POrder order_to_replace, 
                    std::function<Order()> place_request);



    ///place ordinary localy triggered order
    static POrder place_order(PTradableInstrument instrument, 
                const OrderParameters &trig_params,
                POrder order_to_replace);

    static bool convert_params_to_request(const OrderParameters &params, OrderRequest &request);





};
}