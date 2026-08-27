/// Audit harness for SimExecutor order matching math.
/**
 * Each scenario places exactly one order against a fully controlled quote (and
 * optionally a few trades) and records every fill the simulator produced.
 *
 * This is an audit tool, not a regression test: it prints EXPECTED vs ACTUAL
 * for every order type and always exits 0, so one run shows the complete
 * picture instead of stopping at the first mismatch. Once the findings it
 * reports are fixed, the expectations can be turned into hard assertions.
 */
#include "quarkbot/backtest/backtest.hpp"
#include "quarkbot/common/order_internal_defs.hpp"
#include "quarkbot/common/orderdata.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/instrument_description.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/tradable_instrument.hpp"
#include "quarkbot/types.hpp"
#include <array>
#include <chrono>
#include <format>
#include <iostream>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

using namespace quarkbot;

namespace {

constexpr Decimal BID = 100_dec;
constexpr Decimal ASK = 101_dec;

struct FillRec {
    Decimal price;
    Decimal quantity;
};

struct Outcome {
    std::vector<FillRec> fills;
    OrderStatus status = OrderStatus::unknown;
    std::optional<OrderRejectionReason> reason;
    ///every update the report sink saw, in order
    std::vector<std::string> trace;
    ///fill stats read back from the placed orders after the run. Taken from the
    ///order itself, not from the report sink - create_fill() does not
    ///necessarily forward OrderFillStats to the sink on every branch.
    OrderFillStats stats = {};
    ///handles of the placed orders, kept so the stats survive the run
    std::vector<POrder> orders;

    Decimal total_qty() const {
        Decimal q = {};
        for (auto &f: fills) q += f.quantity;
        return q;
    }
    std::string describe() const {
        std::string s;
        for (auto &f: fills) {
            s += std::format("[{} @ {}]", f.quantity.to_string(), f.price.to_string());
        }
        if (s.empty()) s = "(no fill)";
        s += std::format(" status={}", string_lookup<OrderStatus>(status).value_or("?"));
        if (reason) {
            s += std::format(" reason={}", string_lookup<OrderRejectionReason>(*reason).value_or("?"));
        }
        return s;
    }
};

struct Scenario {
    OrderRequest request;
    Decimal bid_size = 10_dec;
    Decimal ask_size = 10_dec;
    ///contract geometry - default is a plain linear contract, multiplier 1
    ContractInfo contract = {};
    Decimal fee_rate_maker = {};
    Decimal fee_rate_taker = {};
    ///how many quote events to replay (the strategy places its order after the first)
    std::size_t quotes = 2;
    ///additional orders placed right after the first one
    std::vector<OrderRequest> also = {};
    ///trade events replayed after the quotes: (price, size)
    std::vector<std::pair<Decimal, Decimal> > trades = {};
};

InstrumentDescription make_desc(const Scenario &sc) {
    return InstrumentDescription{
        sc.contract,
        {0.00001_dec, Decimal::max(), 0.00001_dec, 0.01_dec, 0, 0,
         sc.fee_rate_maker, sc.fee_rate_taker},     //no leverage
        {"USD"}, {"USD"}, {}, "TESTI", InstrumentCategory::Crypto
    };
}

StrategyFragment run_scenario_strategy(StrategyContext ctx, const Scenario *sc, Outcome *out) {
    TradableInstrument instr = ctx.instruments.at(0);
    auto stream = instr.subscribe<Quote>();
    Quote qt;
    do {
        if (!co_await stream.receive(qt)) co_return;
    } while (!qt.both_sides());
    out->orders.push_back(instr.place_order(sc->request).get_handle());
    for (auto &r: sc->also) out->orders.push_back(instr.place_order(r).get_handle());
    co_await ctx.stop_signal;
}

Outcome run_in_thread(const Scenario &sc) {
    auto t0 = std::chrono::system_clock::from_time_t(1780670224);
    Outcome out;

    std::array<WalletInitItem, 1> wallet{WalletInitItem{"USD", 100000000_dec}};
    std::array<InstrumentDescription, 1> instruments{make_desc(sc)};

    SimulationParams sim;
    sim.slippage = 0;   //isolate the matching math from slippage
    sim.reporter = ReportSink([&](const Order &, const OrderStatusUpdate &upd){
        if (std::holds_alternative<Fill>(upd)) {
            const Fill &f = std::get<Fill>(upd);
            out.fills.push_back({f.price, f.quantity});
            out.trace.push_back(std::format("fill({} @ {})",
                        f.quantity.to_string(), f.price.to_string()));
        } else if (std::holds_alternative<OrderStatus>(upd)) {
            out.status = std::get<OrderStatus>(upd);
            out.trace.push_back(std::string(
                        string_lookup<OrderStatus>(out.status).value_or("?")));
        } else if (std::holds_alternative<OrderRejectionReason>(upd)) {
            out.reason = std::get<OrderRejectionReason>(upd);
            //derive through the shared mapping, never hardcode - see
            //order_status_consistency test
            out.status = rejection_reason_2_status(*out.reason);
            out.trace.push_back(std::format("terminated({})",
                        string_lookup<OrderRejectionReason>(*out.reason).value_or("?")));
        } else if (std::holds_alternative<OrderRejectionWithText>(upd)) {
            out.reason = std::get<OrderRejectionWithText>(upd).reason;
            out.status = rejection_reason_2_status(*out.reason);
            out.trace.push_back(std::format("terminated({}: {})",
                        string_lookup<OrderRejectionReason>(*out.reason).value_or("?"),
                        std::get<OrderRejectionWithText>(upd).text));
        } else if (std::holds_alternative<OrderOpenStatus>(upd)) {
            out.trace.push_back("accepted");
        } else if (std::holds_alternative<OrderFillStats>(upd)) {
            const OrderFillStats &st = std::get<OrderFillStats>(upd);
            out.trace.push_back(std::format("stats(filled={} turnover={} fees={} native={})",
                        st.filled.to_string(), st.turnover.to_string(),
                        st.fees.to_string(), st.fees_native.to_string()));
        }
    });

    BacktestEnv bt("backtest", wallet, instruments, sim);
    bt.add_strategy([&](StrategyContext &&c){return run_scenario_strategy(std::move(c), &sc, &out);});

    //events 0..1: quotes (the strategy wakes on the first one and places the order)
    //events 2..N: optional trades
    std::size_t n = 0;
    bt.run([&](BacktestEvent &ev) {
        ev.symbol = "TESTI";
        ev.time = t0 + std::chrono::seconds(static_cast<int>(n));
        if (n < sc.quotes) {
            ev.data = Quote{BID, sc.bid_size, ASK, sc.ask_size, ev.time};
        } else if (n - sc.quotes < sc.trades.size()) {
            auto &[price, size] = sc.trades[n-sc.quotes];
            ev.data = Trade{price, size, ev.time, Side::undetermined};
        } else {
            return false;
        }
        ++n;
        return true;
    });

    //read the authoritative fill stats back from the orders themselves
    for (auto &o: out.orders) {
        auto d = std::dynamic_pointer_cast<OrderInternalData>(o);
        if (!d) continue;
        const OrderFillStats &st = d->get_fill_stats();
        out.stats.filled += st.filled;
        out.stats.turnover += st.turnover;
        out.stats.fees += st.fees;
        out.stats.fees_native += st.fees_native;
    }
    return out;
}

///BacktestExecutor is bound to the thread that created it, so each scenario
///needs a fresh thread of its own
Outcome run(const Scenario &sc) {
    Outcome out;
    std::thread thr([&]{ out = run_in_thread(sc); });
    thr.join();
    return out;
}

int g_diffs = 0;
int g_open = 0;

///Expected result of a scenario; std::nullopt means "not asserted"
struct Expect {
    std::optional<Decimal> total_qty = {};
    std::optional<Decimal> fill_price = {};      //price of the first fill
    std::optional<Decimal> last_fill_price = {}; //price of the last fill
    std::optional<std::size_t> fill_count = {};
    std::optional<OrderStatus> status = {};
    std::optional<Decimal> turnover = {};
    std::optional<Decimal> fees= {};
    std::optional<Decimal> fees_native = {};
    ///known unfixed behaviour: still printed, but does not fail the run
    std::string_view known_open = {};
};

void audit(std::string_view title, std::string_view rationale,
           const Scenario &sc, const Expect &exp) {
    Outcome o = run(sc);
    std::vector<std::string> diffs;

    if (exp.total_qty && *exp.total_qty != o.total_qty()) {
        diffs.push_back(std::format("total qty: expected {}, got {}",
                    exp.total_qty->to_string(), o.total_qty().to_string()));
    }
    if (exp.fill_count && *exp.fill_count != o.fills.size()) {
        diffs.push_back(std::format("fill count: expected {}, got {}",
                    *exp.fill_count, o.fills.size()));
    }
    if (exp.fill_price) {
        if (o.fills.empty()) {
            diffs.push_back(std::format("fill price: expected {}, got no fill",
                        exp.fill_price->to_string()));
        } else if (o.fills.front().price != *exp.fill_price) {
            diffs.push_back(std::format("fill price: expected {}, got {}",
                        exp.fill_price->to_string(), o.fills.front().price.to_string()));
        }
    }
    auto cmp_dec = [&](const char *what, const std::optional<Decimal> &e, Decimal a){
        if (e && *e != a) {
            diffs.push_back(std::format("{}: expected {}, got {}", what, e->to_string(), a.to_string()));
        }
    };
    cmp_dec("turnover", exp.turnover, o.stats.turnover);
    cmp_dec("fees", exp.fees, o.stats.fees);
    cmp_dec("fees_native", exp.fees_native, o.stats.fees_native);
    if (exp.last_fill_price) {
        if (o.fills.empty()) {
            diffs.push_back(std::format("last fill price: expected {}, got no fill",
                        exp.last_fill_price->to_string()));
        } else if (o.fills.back().price != *exp.last_fill_price) {
            diffs.push_back(std::format("last fill price: expected {}, got {}",
                        exp.last_fill_price->to_string(), o.fills.back().price.to_string()));
        }
    }
    if (exp.status && *exp.status != o.status) {
        diffs.push_back(std::format("status: expected {}, got {}",
                    string_lookup<OrderStatus>(*exp.status).value_or("?"),
                    string_lookup<OrderStatus>(o.status).value_or("?")));
    }

    const bool open = !exp.known_open.empty();
    std::cout << (diffs.empty()?"  OK  ":open?" OPEN ":"!!FAIL") << "  " << title << "\n";
    std::cout << "        actual: " << o.describe() << "\n";
    std::cout << "        trace : ";
    for (auto &t: o.trace) std::cout << t << " ";
    std::cout << "\n";
    if (!diffs.empty()) {
        std::cout << "        why expected: " << rationale << "\n";
        for (auto &d: diffs) std::cout << "        > " << d << "\n";
        if (open) {
            std::cout << "        KNOWN OPEN: " << exp.known_open << "\n";
            ++g_open;
        } else {
            ++g_diffs;
        }
    }
}

}

int main() {
    std::cout << "SimExecutor order math audit - quote is bid " << BID.to_string()
              << " / ask " << ASK.to_string() << ", slippage 0, no fees\n\n";

    std::cout << "== MARKET ==\n";
    audit("market buy 4, ask_size 10",
          "enough liquidity: one fill of 4 at the ask",
          {.request = {.side=Side::buy, .type=OrderType::market, .quantity=4_dec}},
          {.total_qty = 4_dec, .fill_price = ASK, .fill_count = 1u});

    audit("market buy 10, ask_size 1 (thin book)",
          "an order can never fill more than its own quantity",
          {.request = {.side=Side::buy, .type=OrderType::market, .quantity=10_dec},
           .ask_size = 1_dec},
          {.total_qty = 10_dec});

    audit("market sell 10, bid_size 1 (thin book)",
          "same as above, mirrored on the sell side",
          {.request = {.side=Side::sell, .type=OrderType::market, .quantity=10_dec},
           .bid_size = 1_dec},
          {.total_qty = 10_dec});

    audit("market buy 10, ask_size 3 (thin book)",
          "an order can never fill more than its own quantity",
          {.request = {.side=Side::buy, .type=OrderType::market, .quantity=10_dec},
           .ask_size = 3_dec},
          {.total_qty = 10_dec});

    audit("market buy 10, ask_size 5 (half the book)",
          "an order can never fill more than its own quantity",
          {.request = {.side=Side::buy, .type=OrderType::market, .quantity=10_dec},
           .ask_size = 5_dec},
          {.total_qty = 10_dec});

    audit("market buy 10, ask_size 9 (slightly thin book)",
          "should be at most two fills, not one per unit",
          {.request = {.side=Side::buy, .type=OrderType::market, .quantity=10_dec},
           .ask_size = 9_dec},
          {.total_qty = 10_dec, .fill_count = 2u});

    std::cout << "\n== LIMIT ==\n";
    audit("limit buy 4 @110 (crosses ask 101)",
          "a crossing buy takes the ask, so it fills at 101 - not at its own limit",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=4_dec, .limit_price=110_dec}},
          {.total_qty = 4_dec, .fill_price = ASK});

    audit("limit sell 4 @90 (crosses bid 100)",
          "a crossing sell hits the bid, so it fills at 100 - not at its own limit",
          {.request = {.side=Side::sell, .type=OrderType::limit,
                       .quantity=4_dec, .limit_price=90_dec}},
          {.total_qty = 4_dec, .fill_price = BID});

    audit("limit buy 4 @101 (equals ask)",
          "fills at the ask",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=4_dec, .limit_price=ASK}},
          {.total_qty = 4_dec, .fill_price = ASK});

    audit("limit buy 10 @110, ask_size 2, three quotes",
          "each event offers 2 again, so the remainder keeps resting and fills 2 per "
          "event. The first fill happens on placement and takes the ask (101); the "
          "later ones are maker fills at the order's own limit (110)",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=10_dec, .limit_price=110_dec},
           .ask_size = 2_dec, .quotes = 3},
          {.total_qty = 6_dec, .fill_price = ASK, .last_fill_price = 110_dec,
           .fill_count = 3u, .status = OrderStatus::unknown});

    audit("limit buy 10 @110, ask_size 0 (feed carries no sizes)",
          "size 0 means unknown, not empty - the order must fill in full",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=10_dec, .limit_price=110_dec},
           .ask_size = 0_dec},
          {.total_qty = 10_dec, .status = OrderStatus::filled});

    audit("market buy 10, ask_size 0 (feed carries no sizes)",
          "size 0 means unknown, so there is enough liquidity - one fill, no book walk",
          {.request = {.side=Side::buy, .type=OrderType::market, .quantity=10_dec},
           .ask_size = 0_dec},
          {.total_qty = 10_dec, .fill_count = 1u, .status = OrderStatus::filled});

    audit("limit buy 4 @90 (passive, below bid)",
          "a passive limit must not fill from a quote alone",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=4_dec, .limit_price=90_dec}},
          {.total_qty = 0_dec});

    audit("limit buy 4 @99, trade @98 size 3",
          "resting buy limit, trade printed through it: fills min(4,3)=3",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=4_dec, .limit_price=99_dec},
           .trades = {{98_dec, 3_dec}}},
          {.total_qty = 3_dec});

    audit("two resting buy limits 10 @110, ask_size 2, two quotes",
          "each order is capped at the quoted size per event, but one order's fill "
          "must not starve the other - otherwise the winner depends on placement order",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=10_dec, .limit_price=110_dec},
           .ask_size = 2_dec,
           .also = {{.side=Side::buy, .type=OrderType::limit,
                     .quantity=10_dec, .limit_price=110_dec}}},
          {.total_qty = 8_dec, .fill_count = 4u});

    std::cout << "\n== LIMIT IOC ==\n";
    audit("limit IOC buy 4 @90 (no cross)",
          "nothing traded, so it is canceled - never reported as filled",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=4_dec, .limit_price=90_dec,
                       .time_in_force=TimeInForce::ioc}},
          {.total_qty = 0_dec, .status = OrderStatus::canceled});

    audit("limit IOC buy 4 @110 (crosses), ask_size 10",
          "enough at the touch, so it fills in full",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=4_dec, .limit_price=110_dec,
                       .time_in_force=TimeInForce::ioc}},
          {.total_qty = 4_dec, .status = OrderStatus::filled});

    audit("limit IOC buy 10 @110 (crosses), ask_size 2",
          "takes the 2 available and cancels the remainder - never rests, so it "
          "must not fill again on the second quote",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=10_dec, .limit_price=110_dec,
                       .time_in_force=TimeInForce::ioc},
           .ask_size = 2_dec},
          {.total_qty = 2_dec, .fill_count = 1u, .status = OrderStatus::canceled});

    std::cout << "\n== LIMIT FOK ==\n";
    audit("limit FOK buy 4 @110 (crosses), ask_size 10",
          "the whole quantity is available, so it fills in full",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=4_dec, .limit_price=110_dec,
                       .time_in_force=TimeInForce::fok}},
          {.total_qty = 4_dec, .fill_count = 1u, .status = OrderStatus::filled});

    audit("limit FOK buy 10 @110 (crosses), ask_size 2",
          "fill or kill must not fill partially - killed with no fill at all",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=10_dec, .limit_price=110_dec,
                       .time_in_force=TimeInForce::fok},
           .ask_size = 2_dec},
          {.total_qty = 0_dec, .fill_count = 0u, .status = OrderStatus::canceled});

    audit("limit FOK buy 4 @90 (no cross)",
          "nothing available, so it is killed",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=4_dec, .limit_price=90_dec,
                       .time_in_force=TimeInForce::fok}},
          {.total_qty = 0_dec, .status = OrderStatus::canceled});

    std::cout << "\n== POST ONLY ==\n";
    audit("post_only buy 4 @110 (would take)",
          "must be refused with post_only_taker - which is not a failure, so it "
          "ends as canceled (see rejection_reason_2_status)",
          {.request = {.side=Side::buy, .type=OrderType::limit_post_only,
                       .quantity=4_dec, .limit_price=110_dec}},
          {.total_qty = 0_dec, .status = OrderStatus::canceled});

    audit("post_only buy 4 @101 (sits exactly on the ask)",
          "only a strictly more aggressive price takes liquidity, so sitting on "
          "the touch is allowed and fills",
          {.request = {.side=Side::buy, .type=OrderType::limit_post_only,
                       .quantity=4_dec, .limit_price=ASK}},
          {.total_qty = 4_dec, .fill_price = ASK, .status = OrderStatus::filled});

    // the point of post_only is the fee tier, so pin it with maker != taker
    audit("post_only buy 4 @101 on the touch, maker 0.001 / taker 0.002",
          "a post_only fill is a maker fill by definition: 0.001 * 4 * 101 = 0.404, "
          "never the taker rate",
          {.request = {.side=Side::buy, .type=OrderType::limit_post_only,
                       .quantity=4_dec, .limit_price=ASK},
           .fee_rate_maker = 0.001_dec, .fee_rate_taker = 0.002_dec},
          {.total_qty = 4_dec, .fees = 0.404_dec});

    audit("plain limit buy 4 @110 crossing, maker 0.001 / taker 0.002",
          "control: an ordinary order that crosses on placement really is the "
          "taker, so 0.002 * 4 * 101 = 0.808",
          {.request = {.side=Side::buy, .type=OrderType::limit,
                       .quantity=4_dec, .limit_price=110_dec},
           .fee_rate_maker = 0.001_dec, .fee_rate_taker = 0.002_dec},
          {.total_qty = 4_dec, .fees = 0.808_dec});

    std::cout << "\n== STOP ==\n";
    audit("stop buy 4, stop@200 (ask 101, far below)",
          "the stop is not reached, so nothing may trade",
          {.request = {.side=Side::buy, .type=OrderType::stop,
                       .quantity=4_dec, .stop_price=200_dec}},
          {.total_qty = 0_dec});

    audit("stop sell 4, stop@50 (bid 100, far above)",
          "the stop is not reached, so nothing may trade",
          {.request = {.side=Side::sell, .type=OrderType::stop,
                       .quantity=4_dec, .stop_price=50_dec}},
          {.total_qty = 0_dec});

    audit("stop buy 4, stop@200, trade @250",
          "the trade passes the stop, so it triggers and fills 4",
          {.request = {.side=Side::buy, .type=OrderType::stop,
                       .quantity=4_dec, .stop_price=200_dec},
           .trades = {{250_dec, 100_dec}}},
          {.total_qty = 4_dec});

    std::cout << "\n== STOPLIMIT ==\n";
    audit("stoplimit buy 4, stop@200 limit@210 (ask 101)",
          "the stop is not reached, so nothing may trade",
          {.request = {.side=Side::buy, .type=OrderType::stoplimit,
                       .quantity=4_dec, .limit_price=210_dec, .stop_price=200_dec}},
          {.total_qty = 0_dec});

    std::cout << "\n== ALERT ==\n";
    audit("alert buy, stop@200 (ask 101, not reached)",
          "must not fire, so it must not reach a final status",
          {.request = {.side=Side::buy, .type=OrderType::alert,
                       .quantity=4_dec, .stop_price=200_dec}},
          {.total_qty = 0_dec, .status = OrderStatus::unknown});

    audit("alert buy, stop@50, quantity 0",
          "an alert generates no fill, so quantity 0 must still be usable",
          {.request = {.side=Side::buy, .type=OrderType::alert,
                       .quantity=0_dec, .stop_price=50_dec}},
          {.total_qty = 0_dec, .status = OrderStatus::filled});

    audit("alert buy, stop@50 (ask 101, passed)",
          "fires immediately and produces no fill",
          {.request = {.side=Side::buy, .type=OrderType::alert,
                       .quantity=4_dec, .stop_price=50_dec}},
          {.total_qty = 0_dec, .fill_count = 0u, .status = OrderStatus::filled});

    std::cout << "\n== CREATE_FILL / CONTRACT GEOMETRY ==\n";
    // linear contract, multiplier 2: turnover and fees must both scale with it
    audit("linear multiplier 2, market buy 4 @101, taker fee 0.001",
          "turnover = 2*4*101 = 808, fees = 0.001*808 = 0.808 in both currencies",
          {.request = {.side=Side::buy, .type=OrderType::market, .quantity=4_dec},
           .contract = {.type = InstrumentType::contract, .multiplier = 2_dec, .tick_scale = 1_dec},
           .fee_rate_taker = 0.001_dec},
          {.total_qty = 4_dec, .turnover = 808_dec,
           .fees = 0.808_dec, .fees_native = 0.808_dec});

    // inverse contract: the two currencies differ, so the two fee fields must too.
    // sell fills at the bid (100), so turnovers stay exact in Decimal.
    audit("inverse contract, market sell 4 @100, taker fee 0.001",
          "pnl turnover = 4/100 = 0.04, quote turnover = 4; "
          "fees(quote) = 0.004, fees_native(pnl) = 0.00004",
          {.request = {.side=Side::sell, .type=OrderType::market, .quantity=4_dec},
           .contract = {.type = InstrumentType::inverse_contract, .multiplier = 1_dec, .tick_scale = 1_dec},
           .fee_rate_taker = 0.001_dec},
          {.total_qty = 4_dec, .turnover = 0.04_dec,
           .fees = 0.004_dec, .fees_native = 0.00004_dec});

    std::cout << "\n" << g_open << " known open issue(s), "
              << g_diffs << " unexpected difference(s)\n";
    if (g_diffs) {
        std::cerr << "FAILED: " << g_diffs
                  << " scenario(s) differ from expectation and are not marked KNOWN OPEN\n";
        return 1;
    }
    std::cout << "order type math OK\n";
    return 0;
}
