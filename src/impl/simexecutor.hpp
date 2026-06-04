#pragma once

#include "ifc/abstract/orderdata.hpp"
#include "ifc/defs.hpp"
#include "ifc/order_defs.hpp"
#include "ifc/strategy_fragment.hpp"
#include "ifc/stream/auction.hpp"
#include "ifc/stream/trade.hpp"
#include "ifc/stream/quote.hpp"
#include "ifc/scope_counter.hpp"
#include "ifc/order.hpp"
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
    void on_event(PSimInstrument instrument, Auction &auction);

    StrategyFragment place_order(POrderAData ord);
    StrategyFragment replace_order(POrderAData ord, POrderAData prev_order);
    StrategyFragment cancel_order(POrderAData ord);
    StrategyFragment cancel_order(OrderInternalData *ord);
    bool cancel_all(PTradableInstrument instrument);
    void set_slippage(double slippage) { _slippage = slippage; }
    void set_latency(std::chrono::system_clock::duration dur) {latency = dur;}

    ~SimExecutor();

protected:

    std::chrono::system_clock::duration latency = {};
    double _slippage = 0.001;

    struct ActiveOrder {
        POrderAData ord;
        PSimInstrument instrument;
        Decimal filled = {};
        TimeInForce time_in_force;
        bool trig = false;
    };

    std::vector<ActiveOrder> _active_orders;    
    std::optional<Quote> _last_quote;
    std::uint64_t _random_key = 0;

    bool validate_order(ActiveOrder &order);
    bool validate_order_replace(ActiveOrder &order, const ActiveOrder &replacing_order);
    bool match_order(ActiveOrder &order, bool taker);
    bool match_order(ActiveOrder &order, Quote &quote, bool taker);
    bool match_order(ActiveOrder &order, Trade &trade);
    void create_fill(ActiveOrder &order, Decimal price, Decimal quantity, Timestamp tp, bool taker);

    OrderType real_order_type(const ActiveOrder &order);

    static PSimInstrument extract_instrument(const POrderAData &ord);

    void set_order_status(const POrderAData &ord, OrderInternalData::Update &&st);
    void accept_order(const POrderAData &ord);
    
    Timer _latency_timer;


    void place_order_internal(POrderAData ord);
    void place_order_internal(POrderAData ord, POrderAData prev_order);
    void cancel_order_internal(OrderInternalData *ord);
    void stop_latency_queue();
    //TODO implement reporting of order blocking
};


}
