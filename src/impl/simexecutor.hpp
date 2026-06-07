#pragma once

#include "ifc/abstract/orderdata.hpp"
#include "ifc/defs.hpp"
#include "ifc/order_defs.hpp"
#include "ifc/strategy_fragment.hpp"
#include "ifc/stream/auction.hpp"
#include "ifc/stream/trade.hpp"
#include "ifc/stream/quote.hpp"
#include "ifc/timer.hpp"
#include <chrono>
#include <memory>
#include <random>
#include <unordered_map>
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

    using ReportSink = std::function<void(const POrderAData &, const OrderInternalData::Update &)>;

    void set_report_sink(ReportSink rpt) {
        _report_sink = std::move(rpt);
    }

protected:

    std::chrono::system_clock::duration latency = {};
    double _slippage = 0.001;

    ReportSink _report_sink;

    struct ActiveOrder {
        POrderAData ord;
        PSimInstrument instrument;
        Decimal filled = {};
        TimeInForce time_in_force;
        bool trig = false;
    };


    struct AuctionState {
        //at open auction started - found auction event in phase at open
        //you can place any other you want
        bool at_open_started = false;
        //at open auction finished - either by final trade, or by first trade from continuous trading phase
        //ato orders are rejected
        bool at_open_finished = false;
        //at close auction started - found auction event
        //day orders are rejected
        bool at_close_started = false;
        //at close auction finished - either by final trade, or by expiration after certain period (day is closed)
        //all orders except GTC are closed and rejected
        bool at_close_finished = false;
    };

    std::unordered_map<std::string, AuctionState> _auction_state;

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
    
    Timer _timer;    
    std::default_random_engine _rnd_gen;


    void place_order_internal(POrderAData ord);
    void place_order_internal(POrderAData ord, POrderAData prev_order);
    void cancel_order_internal(OrderInternalData *ord);
    void stop_latency_queue();
    void close_day(PSimInstrument instrument);


    StrategyFragment expire_auction(PSimInstrument instrument, std::chrono::system_clock::time_point tp);
    void seed_random(std::chrono::system_clock::time_point tp);
};


}
