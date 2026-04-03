#pragma once

#include "ifc/market_events.hpp"
#include "ifc/order.hpp"
#include <chrono>
namespace quarkbot {

class SimExecutor {
public:

    using Timestamp = std::chrono::system_clock::time_point;
    
    class IExecutionResult {
    public:
        virtual ~IExecutionResult() = default;
        virtual void report_fill(const Order &ord, const Fill &fill) = 0;
        virtual void report_status(const Order &ord, const OrderStatusUpdate &status) = 0;        
        virtual void init(const Order &ord, const OrderInitialUpdate &init) = 0;        
    };


    void on_event(std::string_view instrument, Trade &trade);
    void on_event(std::string_view instrument, Quote &quote);

    void place_order(Order ord, std::string instrument, IExecutionResult *result);
    void replace_order(Order ord, Order prev_order, std::string instrument, IExecutionResult *result);
    void cancel_order(Order ord);

protected:

    double _slippage = 0.001;
    double _maker_fees = 0.001;
    double _taker_fees = 0.002;

    struct ActiveOrder {
        Order ord;
        std::string instrument;       
        IExecutionResult *result; //extracted from PTradableInstrument but not indirectly accessible from here, so we need pointer
        Decimal filled = {};
        bool trig = false;
    };

    std::vector<ActiveOrder> _active_orders;
    std::optional<Quote> _last_quote;

    bool validate_order(ActiveOrder &order);
    bool validate_order_replace(ActiveOrder &order, const ActiveOrder &replacing_order);
    bool match_order(ActiveOrder &order, bool taker);
    bool match_order(ActiveOrder &order, Quote &quote, bool taker);
    bool match_order(ActiveOrder &order, Trade &trade);
    void create_fill(ActiveOrder &order, Decimal price, Decimal quantity, Timestamp tp, bool taker);

    OrderType real_order_type(const ActiveOrder &order);
};


}