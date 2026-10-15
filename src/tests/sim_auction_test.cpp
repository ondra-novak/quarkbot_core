/// SimExecutor auction behaviour, end to end through BacktestEnv
/**
 * Each scenario replays a fixed list of events. The strategy places its orders
 * when it receives the first quote, and the report sink records the outcome of
 * every order by its label.
 */
#include "check.h"
#include "quarkbot/backtest/backtest.hpp"
#include "quarkbot/common/order_internal_defs.hpp"
#include "quarkbot/common/orderdata.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/instrument_description.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/stream/auction.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/tradable_instrument.hpp"
#include "quarkbot/types.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace quarkbot;
using namespace std::chrono_literals;

namespace {

using Event = std::variant<Quote, Trade, Auction>;

struct TimedEvent {
    ///offset from the start of the scenario
    std::chrono::seconds at;
    Event ev;
};

struct FillRec {
    Decimal price;
    Decimal quantity;
};

struct Outcome {
    std::vector<FillRec> fills;
    ///last plain status reported (filled, canceled...)
    std::optional<OrderStatus> status;
    ///termination reason (expired, too_late...)
    std::optional<OrderRejectionReason> reason;
    bool terminated() const {
        return reason.has_value()
            || (status.has_value() && *status != OrderStatus::open);
    }
};

using Outcomes = std::map<std::string, Outcome>;

struct Scenario {
    std::vector<TimedEvent> events;
    ///placed when the strategy receives the first quote; labels must be unique
    std::vector<OrderRequest> orders;
};

Quote quote(Decimal bid, Decimal ask) {
    return Quote{bid, 10_dec, ask, 10_dec, {}};
}

Trade trade(Decimal price, Decimal size, TradeQualifier q = TradeQualifier::continuous) {
    return Trade{price, size, {}, Side::undetermined, q};
}

Auction indicative(AuctionType type, Decimal price) {
    Auction a;
    a.price = price;
    a.quantity = 100_dec;
    a.imbalance = 0_dec;
    a.auction_type = type;
    return a;
}

OrderRequest buy(std::string label, OrderType type, Decimal qty, TimeInForce tif,
                 Decimal limit = {}, Decimal stop = {}) {
    return OrderRequest{
        .label = std::move(label), .side = Side::buy, .type = type, .quantity = qty,
        .limit_price = limit, .stop_price = stop, .time_in_force = tif
    };
}

InstrumentDescription make_desc() {
    return InstrumentDescription{
        {},
        {0.00001_dec, Decimal::max(), 0.00001_dec, 0.01_dec, 0, 0, 0, 0},
        {"USD"}, {"USD"}, {}, "TESTI", InstrumentCategory::Crypto
    };
}

StrategyFragment strategy(StrategyContext ctx, const Scenario *sc) {
    TradableInstrument instr = ctx.instruments.at(0);
    auto stream = instr.subscribe<Quote>();
    Quote qt;
    if (!co_await stream.receive(qt)) co_return;
    //keep the instances alive - a dropped order instance is canceled
    std::vector<Order> keep;
    for (auto &r: sc->orders) keep.push_back(instr.place_order(r));
    co_await ctx.stop_signal;
}

Outcomes run_in_thread(const Scenario &sc) {
    auto t0 = std::chrono::system_clock::from_time_t(1780670224);
    Outcomes out;

    std::array<WalletInitItem, 1> wallet{WalletInitItem{"USD", 100000000_dec}};
    std::array<InstrumentDescription, 1> instruments{make_desc()};

    SimulationParams sim;
    sim.slippage = 0;
    sim.reporter = ReportSink([&](const Order &ord, const OrderStatusUpdate &upd){
        Outcome &o = out[ord.get_parameters().label];
        if (std::holds_alternative<Fill>(upd)) {
            const Fill &f = std::get<Fill>(upd);
            o.fills.push_back({f.price, f.quantity});
        } else if (std::holds_alternative<OrderStatus>(upd)) {
            o.status = std::get<OrderStatus>(upd);
        } else if (std::holds_alternative<OrderRejectionReason>(upd)) {
            o.reason = std::get<OrderRejectionReason>(upd);
        } else if (std::holds_alternative<OrderRejectionWithText>(upd)) {
            o.reason = std::get<OrderRejectionWithText>(upd).reason;
        }
    });

    BacktestEnv bt("backtest", wallet, instruments, sim);
    bt.add_strategy([&](StrategyContext &&c){return strategy(std::move(c), &sc);});

    std::size_t n = 0;
    bt.run([&](BacktestEvent &ev) {
        if (n >= sc.events.size()) return false;
        const TimedEvent &te = sc.events[n++];
        ev.symbol = "TESTI";
        ev.time = t0 + te.at;
        std::visit([&](auto e){
            e.time = ev.time;
            ev.data = e;
        }, te.ev);
        return true;
    });
    return out;
}

///BacktestExecutor is bound to the thread that created it, so each scenario
///needs a fresh thread of its own
Outcomes run(const Scenario &sc) {
    Outcomes out;
    std::thread thr([&]{ out = run_in_thread(sc); });
    thr.join();
    return out;
}

void check_fills(const Outcomes &out, const std::string &label, const std::vector<FillRec> &expected) {
    std::cout << "    order " << label << std::endl;
    auto iter = out.find(label);
    CHECK(iter != out.end());
    const auto &fills = iter->second.fills;
    CHECK_EQUAL(fills.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK_EQUAL(fills[i].price, expected[i].price);
        CHECK_EQUAL(fills[i].quantity, expected[i].quantity);
    }
}

const Outcome &get(const Outcomes &out, const std::string &label) {
    auto iter = out.find(label);
    CHECK(iter != out.end());
    return iter->second;
}

}

///Algoseek / IQFeed style: no indicative data, the opening print is the auction
static void test_opening_without_indicative() {
    std::cout << "--- test_opening_without_indicative" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,   trade(99_dec, 100_dec, TradeQualifier::closing_auction)},   //previous close
        {60s,  quote(100_dec, 101_dec)},                                   //closed: orders placed
        {120s, trade(100_dec, 5_dec, TradeQualifier::opening_auction)},    //uncross at 100, S = 5
        {121s, trade(100_dec, 3_dec, TradeQualifier::opening_auction)},    //duplicate print
        {122s, quote(100_dec, 101_dec)},
    };
    sc.orders = {
        buy("ato_mkt", OrderType::market, 7_dec, TimeInForce::ato),
        buy("ato_lim_miss", OrderType::limit, 5_dec, TimeInForce::ato, 99_dec),
        buy("atc_lim", OrderType::limit, 5_dec, TimeInForce::atc, 105_dec),
        buy("gtc_lim", OrderType::limit, 8_dec, TimeInForce::gtc, 102_dec),
        buy("gtc_mkt", OrderType::market, 20_dec, TimeInForce::gtc),
        buy("gtc_stop", OrderType::stop, 3_dec, TimeInForce::gtc, {}, 99.5_dec),
        buy("alert", OrderType::alert, 1_dec, TimeInForce::gtc, {}, 99.5_dec),
        buy("day_lim", OrderType::limit, 1_dec, TimeInForce::day, 102_dec),
    };
    auto out = run(sc);

    //auction order: full quantity at the auction price, no volume cap for market
    check_fills(out, "ato_mkt", {{100_dec, 7_dec}});
    CHECK(get(out, "ato_mkt").status == OrderStatus::filled);
    //limit below the auction price: no fill, expired with the auction
    check_fills(out, "ato_lim_miss", {});
    CHECK(get(out, "ato_lim_miss").reason == OrderRejectionReason::expired);
    //closing order placed while the day is closed - it cannot take part in anything
    check_fills(out, "atc_lim", {});
    CHECK(get(out, "atc_lim").reason == OrderRejectionReason::too_late);
    //regular limit: capped by the first print at the auction price, then the
    //duplicate is a continuous trade filled at the limit price
    check_fills(out, "gtc_lim", {{100_dec, 5_dec}, {102_dec, 3_dec}});
    //regular market: no volume cap
    check_fills(out, "gtc_mkt", {{100_dec, 20_dec}});
    //stop: triggered by the auction price, filled in continuous trading at the ask
    check_fills(out, "gtc_stop", {{101_dec, 3_dec}});
    //alert: triggered by the auction price, no fill
    check_fills(out, "alert", {});
    CHECK(get(out, "alert").status == OrderStatus::filled);
    //day order while the day is closed
    check_fills(out, "day_lim", {});
    CHECK(get(out, "day_lim").reason == OrderRejectionReason::too_late);
}

///indicative data drives the call phase - nothing is matched until the uncross
static void test_call_phase() {
    std::cout << "--- test_call_phase" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,  trade(99_dec, 100_dec, TradeQualifier::closing_auction)},
        {60s, indicative(AuctionType::opening, 100_dec)},                  //new day, call
        {61s, quote(100_dec, 101_dec)},                                    //orders placed
        {62s, quote(100_dec, 101_dec)},                                    //still call
        {64s, trade(100.5_dec, 10_dec, TradeQualifier::opening_auction)},
    };
    sc.orders = {
        buy("gtc_lim", OrderType::limit, 4_dec, TimeInForce::gtc, 102_dec),
        buy("ioc_lim", OrderType::limit, 1_dec, TimeInForce::ioc, 102_dec),
        buy("ato_lim", OrderType::limit, 2_dec, TimeInForce::ato, 101_dec),
    };
    auto out = run(sc);

    //would cross the ask 101 in continuous trading - in the call phase it waits
    check_fills(out, "gtc_lim", {{100.5_dec, 4_dec}});
    check_fills(out, "ioc_lim", {});
    CHECK(get(out, "ioc_lim").reason == OrderRejectionReason::not_filled);
    check_fills(out, "ato_lim", {{100.5_dec, 2_dec}});
    CHECK(get(out, "ato_lim").status == OrderStatus::filled);
}

///the opening call ends with a continuous trade - the auction did not happen
static void test_opening_did_not_happen() {
    std::cout << "--- test_opening_did_not_happen" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,  trade(99_dec, 100_dec, TradeQualifier::closing_auction)},
        {60s, indicative(AuctionType::opening, 100_dec)},
        {61s, quote(100_dec, 101_dec)},
        {62s, trade(101_dec, 5_dec)},
        {63s, quote(100_dec, 101_dec)},
    };
    sc.orders = {
        buy("ato_lim", OrderType::limit, 2_dec, TimeInForce::ato, 101_dec),
        buy("gtc_lim", OrderType::limit, 1_dec, TimeInForce::gtc, 102_dec),
    };
    auto out = run(sc);

    check_fills(out, "ato_lim", {});
    CHECK(get(out, "ato_lim").reason == OrderRejectionReason::expired);
    //the first continuous trade is matched as usual - at the limit price
    check_fills(out, "gtc_lim", {{102_dec, 1_dec}});
}

///closing auction with indicative data, close of the day, duplicate print
static void test_closing_auction() {
    std::cout << "--- test_closing_auction" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,  trade(99_dec, 100_dec, TradeQualifier::opening_auction)},
        {1s,  quote(100_dec, 101_dec)},                                    //orders placed
        {5s,  trade(101_dec, 5_dec)},                                      //continuous
        {10s, indicative(AuctionType::closing, 100_dec)},
        {20s, trade(100_dec, 50_dec, TradeQualifier::closing_auction)},
        {21s, trade(100_dec, 50_dec, TradeQualifier::closing_auction)},    //duplicate
        {30s, quote(80_dec, 85_dec)},                                      //closed
    };
    sc.orders = {
        buy("atc_mkt", OrderType::market, 3_dec, TimeInForce::atc),
        buy("ato_late", OrderType::market, 1_dec, TimeInForce::ato),
        buy("day_far", OrderType::limit, 1_dec, TimeInForce::day, 90_dec),
        buy("gtc_far", OrderType::limit, 1_dec, TimeInForce::gtc, 90_dec),
        buy("atc_lim", OrderType::limit, 1_dec, TimeInForce::atc, 105_dec),
    };
    auto out = run(sc);

    //a fill at 105 would mean the continuous trade matched an auction order
    check_fills(out, "atc_lim", {{100_dec, 1_dec}});
    CHECK(get(out, "atc_lim").status == OrderStatus::filled);
    check_fills(out, "atc_mkt", {{100_dec, 3_dec}});
    CHECK(get(out, "atc_mkt").status == OrderStatus::filled);
    check_fills(out, "ato_late", {});
    CHECK(get(out, "ato_late").reason == OrderRejectionReason::too_late);
    //close_day expires day orders
    check_fills(out, "day_far", {});
    CHECK(get(out, "day_far").reason == OrderRejectionReason::expired);
    //gtc survives, and the crossing quote after the close does not fill it
    check_fills(out, "gtc_far", {});
    CHECK(!get(out, "gtc_far").terminated());
}

///without a closing trade the timeout closes the day
static void test_closing_timeout() {
    std::cout << "--- test_closing_timeout" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,          trade(99_dec, 100_dec, TradeQualifier::opening_auction)},
        {1s,          quote(100_dec, 101_dec)},                            //orders placed
        {10s,         indicative(AuctionType::closing, 100_dec)},
        {29min,       quote(100_dec, 101_dec)},
        {31min,       quote(100_dec, 101_dec)},                            //after the timeout
    };
    sc.orders = {
        buy("day_far", OrderType::limit, 1_dec, TimeInForce::day, 90_dec),
        buy("atc_lim", OrderType::limit, 1_dec, TimeInForce::atc, 95_dec),
    };
    auto out = run(sc);

    check_fills(out, "day_far", {});
    CHECK(get(out, "day_far").reason == OrderRejectionReason::expired);
    check_fills(out, "atc_lim", {});
    CHECK(get(out, "atc_lim").reason == OrderRejectionReason::expired);
}

///an unscheduled auction runs once; a duplicate print within the window is continuous
static void test_unscheduled_duplicate() {
    std::cout << "--- test_unscheduled_duplicate" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,  trade(100_dec, 10_dec, TradeQualifier::opening_auction)},
        {1s,  quote(100_dec, 101_dec)},                                    //orders placed
        {10s, trade(99_dec, 4_dec, TradeQualifier::unscheduled_auction)},
        {20s, trade(99_dec, 4_dec, TradeQualifier::unscheduled_auction)},  //duplicate
    };
    sc.orders = {
        buy("crs_lim", OrderType::limit, 10_dec, TimeInForce::crossing, 100_dec),
        buy("gtc_lim", OrderType::limit, 10_dec, TimeInForce::gtc, 99.5_dec),
    };
    auto out = run(sc);

    //crossing joins any auction; the remainder expires with it
    check_fills(out, "crs_lim", {{99_dec, 4_dec}});
    CHECK(get(out, "crs_lim").reason == OrderRejectionReason::expired);
    //auction price first, then the duplicate as a continuous trade at the limit
    check_fills(out, "gtc_lim", {{99_dec, 4_dec}, {99.5_dec, 4_dec}});
}

///an instrument without any auction event trades exactly as before
static void test_no_auctions() {
    std::cout << "--- test_no_auctions" << std::endl;
    Scenario sc;
    sc.events = {
        {0s, quote(100_dec, 101_dec)},                                     //orders placed
        {1s, quote(100_dec, 101_dec)},
    };
    sc.orders = {
        buy("ato_mkt", OrderType::market, 1_dec, TimeInForce::ato),
        buy("gtc_lim", OrderType::limit, 1_dec, TimeInForce::gtc, 102_dec),
    };
    auto out = run(sc);

    check_fills(out, "ato_mkt", {});
    CHECK(get(out, "ato_mkt").reason == OrderRejectionReason::not_tradable);
    check_fills(out, "gtc_lim", {{101_dec, 1_dec}});
}

int main() {
    test_opening_without_indicative();
    test_call_phase();
    test_opening_did_not_happen();
    test_closing_auction();
    test_closing_timeout();
    test_unscheduled_duplicate();
    test_no_auctions();
    return 0;
}
