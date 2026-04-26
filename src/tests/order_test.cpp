#include "impl/simexchange.hpp"
#include "impl/backtest_executor.hpp"
#include "ifc/backtest_data_source.hpp"
#include "ifc/order.hpp"
#include "ifc/context.hpp"
#include "ifc/tradable_instrument.hpp"
#include "basic_coro/sync_await.hpp"
#include "tests/check.h"
#include <memory>
#include <vector>

using namespace quarkbot;

struct OrderFixture {
    std::shared_ptr<BacktestExecutor>     executor;
    std::shared_ptr<SimExchange>          exchange;
    PAccount                              account;
    PTradableInstrument                   instrument;
    std::chrono::system_clock::time_point t0 = std::chrono::system_clock::now();

    OrderFixture() {
        executor = std::make_shared<BacktestExecutor>();
        executor->attach_to_thread();

        exchange = std::make_shared<SimExchange>();

        InstrumentSpec spec;
        spec.name              = "BTCUSD";
        spec.type              = InstrumentType::spot;
        spec.quote_currency    = "USD";
        spec.pnl_currency      = "USD";
        spec.asset_wallet      = "BTC";
        spec.min_lot_size      = Decimal(1, -5);
        spec.lot_size_increment= Decimal(1, -5);
        spec.price_increment   = Decimal(1, -2);
        spec.fee_rate_maker    = Decimal(1, -3);
        spec.fee_rate_taker    = Decimal(2, -3);
        spec.multiplier        = Decimal(1);
        spec.tick_scale        = Decimal(1);

        auto info  = spec.resolve(*exchange);
        auto minstr = exchange->create_instrument(info);

        std::vector<std::pair<std::string, Decimal>> wallet = {{"USD", 10000_dec}};
        account    = exchange->create_account("test", wallet);
        instrument = minstr->create_tradable_instrument(account).get();
    }
};

// Drain the initial "open" status update that accept_order() always queues.
// Safe because the update is already in the queue before this is called.
static void drain_status(Order &order) {
    coro::sync_await(order.next_event());
    while (order.any_fill()) order.read_fill();
}

// Consume all updates until the order reaches a terminal state.
// Only call this after an on_event() that is expected to close the order.
static void drain_until_done(Order &order) {
    while (order.any_fill()) order.read_fill();
    bool cont = coro::sync_await(order.next_event());
    while (cont) {
        while (order.any_fill()) order.read_fill();
        cont = coro::sync_await(order.next_event());
    }
}

// --- placeholder tests (added in later tasks) ---

int main() {
    return 0;
}
