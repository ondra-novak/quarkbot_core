#pragma once

#include "../common/orderdata.hpp"
#include "quarkbot/abstract/backtest_data_source.hpp"
#include <quarkbot/defs.hpp>
#include <quarkbot/order_defs.hpp>
#include <quarkbot/strategy_fragment.hpp>
#include <quarkbot/stream/auction.hpp>
#include <quarkbot/stream/trade.hpp>
#include <quarkbot/stream/quote.hpp>
#include <quarkbot/timer.hpp>
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

    StrategyFragment place_order(POrder ord);
    StrategyFragment replace_order(POrder ord, POrder prev_order);
    StrategyFragment cancel_order(POrder ord);
    StrategyFragment cancel_order(IOrder *ord);
    bool cancel_all(PTradableInstrument instrument);
    void set_slippage(double slippage) { _slippage = slippage; }
    void set_latency(std::chrono::system_clock::duration dur) {latency = dur;}
    void stop_on(std::stop_token tkn);

    void set_report_sink(ReportSink rpt) {
        _report_sink = std::move(rpt);
    }

protected:

    std::chrono::system_clock::duration latency = {};
    double _slippage = 0.001;

    ReportSink _report_sink;

    struct ActiveOrder {
        POrder ord;
        PSimInstrument instrument;
        TimeInForce time_in_force;
        OrderFillStats calcs;
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
    ///last seen quote per instrument, keyed by instrument name
    /** Must be per instrument - an order may only ever be matched against market
     *  data of its own instrument. Keyed by name (not by SimInstrument pointer)
     *  because instances are cached through a weak_ptr and can be recreated,
     *  while the name is the stable identity - same as _auction_state.
     */
    std::unordered_map<std::string, Quote> _last_quote;
    std::uint64_t _random_key = 0;

    bool validate_order(ActiveOrder &order);
    bool validate_order_replace(ActiveOrder &order, const ActiveOrder &replacing_order);
    bool match_order(ActiveOrder &order, bool taker);
    ///Match one order against a quote
    /** The quote is taken by value on purpose: an order consumes quoted size as
     *  it walks its own fills (which caps it at the quoted size per event), but
     *  that consumption must not change what other orders see. Only L1 is
     *  available, so queue competition between our own same-side orders cannot
     *  be modelled - and sharing one snapshot would make the outcome depend on
     *  the order of _active_orders, which carries no economic meaning.
     *  Consequence: aggregate fills across several same-side orders can exceed
     *  the displayed size. Market impact is not modelled.
     */
    bool match_order(ActiveOrder &order, Quote quote, bool taker);
    bool match_order(ActiveOrder &order, Trade &trade);
    void create_fill(ActiveOrder &order, Decimal price, Decimal quantity, Timestamp tp, bool taker);

    OrderType real_order_type(const ActiveOrder &order);
    Decimal slipped_price(Decimal price, Side side) const;

    static PSimInstrument extract_instrument(const POrder &ord);

    void set_order_status(const POrder &ord, OrderInternalData::Update &&st);
    void accept_order(const POrder &ord);
    
    Timer _timer;    
    std::default_random_engine _rnd_gen;


    void place_order_internal(POrder ord);
    void place_order_internal(POrder ord, POrder prev_order);
    void cancel_order_internal(IOrder *ord);
    void stop_latency_queue();
    void close_day(PSimInstrument instrument);


    StrategyFragment expire_auction(PSimInstrument instrument, std::chrono::system_clock::time_point tp);
    void seed_random(std::chrono::system_clock::time_point tp);
};


}
