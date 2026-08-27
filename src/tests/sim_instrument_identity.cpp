#include "check.h"
#include "quarkbot/backtest/simexchange.hpp"
#include "quarkbot/backtest/siminstrument.hpp"
#include "quarkbot/instrument_description.hpp"
#include "quarkbot/types.hpp"
#include <memory>

using namespace quarkbot;

static InstrumentDescription make_desc() {
    return InstrumentDescription{
        {},
        {0.00001_dec, Decimal::max(), 0.00001_dec, 1, 10, 10, 0, 0},
        {"USD"},
        {"USD"},
        {},
        "BTCUSD",
        InstrumentCategory::Crypto
    };
}

///SimInstrument must be unique per name - SimExecutor compares instruments by pointer
int main() {
    auto ex = std::make_shared<SimExchange>();
    //BacktestEnv discards the handle returned by add_instrument()
    ex->add_instrument(make_desc());

    //this is what init_context_basic() does - the strategy holds this instance
    auto all = ex->get_market_instruments();
    CHECK_EQUAL(all.size(), 1u);
    auto from_list = all[0].get_handle();

    //repeated enumeration must yield the same instance
    auto all2 = ex->get_market_instruments();
    CHECK_EQUAL(from_list.get(), all2[0].get_handle().get());

    //create_instrument() goes through resolve_instrument() - the very same path
    //SimExchange::on_event() uses to dispatch market data to SimExecutor
    auto resolved = ex->create_instrument("BTCUSD", InstrumentType::contract);
    CHECK_EQUAL(from_list.get(), resolved.get());

    //streams are keyed by name and outlive the SimInstrument (json_report.cpp
    //subscribes from a temporary handle), so on_event() must keep feeding them
    //even when no instance is alive
    auto stream = MarketInstrument(from_list).subscribe<Quote>();
    from_list.reset();
    all.clear();
    all2.clear();
    resolved.reset();

    Quote qt = {};
    qt.bid = 100_dec;
    qt.ask = 101_dec;
    qt.bid_size = 1_dec;
    qt.ask_size = 1_dec;
    qt.time = std::chrono::system_clock::now();
    ex->on_event("BTCUSD", qt);

    Quote received = {};
    CHECK(stream.current(received));
    CHECK_EQUAL(received.bid, 100_dec);
    CHECK_EQUAL(received.ask, 101_dec);

    return 0;
}
