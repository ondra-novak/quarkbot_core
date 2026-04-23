#pragma once

#include "ifc/defs.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/order.hpp"
#include "ifc/underlying.hpp"
#include "impl/simaccount.hpp"
#include <chrono>
#include <memory>
namespace quarkbot {

class SimInstrument;
class SimTradableInstrument;


class SimExecutor {
public:

    using Timestamp = std::chrono::system_clock::time_point;


    using PSimInstrument = std::shared_ptr<SimInstrument>;

    void on_event(PSimInstrument instrument, Trade &trade);
    void on_event(PSimInstrument instrument, Quote &quote);

    void place_order(Order ord);
    void replace_order(Order ord, Order prev_order);
    void cancel_order(Order ord);
    bool cancel_all(PTradableInstrument instrument);
    void set_reporter(PReporter reporter);


protected:

    double _slippage = 0.001;
    double _maker_fees = 0.001;
    double _taker_fees = 0.002;

    struct ActiveOrder {
        Order ord;
        PSimInstrument instrument;
        Decimal filled = {};
        bool trig = false;
    };

    std::vector<ActiveOrder> _active_orders;
    std::optional<Quote> _last_quote;
    PReporter _reporter;

    bool validate_order(ActiveOrder &order);
    bool validate_order_replace(ActiveOrder &order, const ActiveOrder &replacing_order);
    bool match_order(ActiveOrder &order, bool taker);
    bool match_order(ActiveOrder &order, Quote &quote, bool taker);
    bool match_order(ActiveOrder &order, Trade &trade);
    void create_fill(ActiveOrder &order, Decimal price, Decimal quantity, Timestamp tp, bool taker);

    OrderType real_order_type(const ActiveOrder &order);

    static PSimInstrument extract_instrument(const Order &ord);

    void set_order_status(const Order &ord, const OrderStatusUpdate &st);
    void accept_order(const Order &ord);

    //TODO implement reporting of order blocking
};


}
