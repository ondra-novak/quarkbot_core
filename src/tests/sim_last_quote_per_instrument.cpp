#include "check.h"
#include "quarkbot/backtest/backtest.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/instrument_description.hpp"
#include "quarkbot/common/orderdata.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/tradable_instrument.hpp"
#include <array>
#include <chrono>
#include <map>
#include <variant>

using namespace quarkbot;

static InstrumentDescription make_desc(std::string name) {
    return InstrumentDescription{
        {},
        {0.00001_dec, Decimal::max(), 0.00001_dec, 0.01_dec, 0, 0, 0, 0},
        {"USD"},
        {"USD"},
        {},
        std::move(name),
        InstrumentCategory::Crypto
    };
}

///Places a market order on QUOTED and on OTHER; only QUOTED ever gets a quote
static StrategyFragment strategy(StrategyContext ctx) {
    TradableInstrument quoted;
    TradableInstrument other;
    for (auto &i: ctx.instruments) {
        if (i.get_info().name == "QUOTED") quoted = i;
        else other = i;
    }
    CHECK(static_cast<bool>(quoted));
    CHECK(static_cast<bool>(other));

    //wait until QUOTED has a two-sided quote, so SimExecutor has seen one quote
    auto stream = quoted.subscribe<Quote>();
    Quote qt;
    do {
        if (!co_await stream.receive(qt)) co_return;
    } while (!qt.both_sides());

    //OTHER never received any quote - this order must stay pending
    other.place_order(OrderRequest{
        .side = Side::buy, .type = OrderType::market, .quantity = 1_dec
    });
    //control: QUOTED did receive a quote - this one must fill
    quoted.place_order(OrderRequest{
        .side = Side::buy, .type = OrderType::market, .quantity = 1_dec
    });

    //don't await the orders - a market order on OTHER must never complete,
    //so awaiting it would hide the control order's result
    co_await ctx.stop_signal;
}

int main() {
    auto t0 = std::chrono::system_clock::from_time_t(1780670224);

    ///fills observed per instrument name, collected through the report sink so
    ///the assertions do not depend on coroutine completion order
    std::map<std::string, Decimal> fills;

    std::array<WalletInitItem, 1> wallet{WalletInitItem{"USD", 1000000_dec}};
    std::array<InstrumentDescription, 2> instruments{
        make_desc("QUOTED"), make_desc("OTHER")
    };

    SimulationParams sim;
    sim.reporter = ReportSink([&](const Order &ord, const OrderStatusUpdate &upd){
        if (std::holds_alternative<Fill>(upd)) {
            const Fill &f = std::get<Fill>(upd);
            fills[ord.get_instrument().get_info().name] = f.price;
        }
    });

    BacktestEnv bt("backtest", wallet, instruments, sim);
    bt.add_strategy([](StrategyContext &&c){return strategy(std::move(c));});

    //feed quotes for QUOTED only
    int n = 0;
    bt.run([&](BacktestEvent &ev) {
        if (n >= 5) return false;
        ev.symbol = "QUOTED";
        ev.time = t0 + std::chrono::seconds(n);
        ev.data = Quote{500_dec, 10_dec, 501_dec, 10_dec, ev.time};
        ++n;
        return true;
    });

    //the control order on QUOTED must have been filled - proves the harness works
    CHECK(fills.contains("QUOTED"));
    //OTHER has no market data at all, so its order must never fill.
    //Before the fix it filled at 501 - the ask of QUOTED.
    if (fills.contains("OTHER")) {
        std::cerr << "OTHER filled at price " << fills["OTHER"].to_string() << "\n";
    }
    CHECK(!fills.contains("OTHER"));
    return 0;
}
