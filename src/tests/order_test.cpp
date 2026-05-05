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
        spec.min_lot_size      = 1_dec;
        spec.lot_size_increment= 1_dec;
        spec.price_increment   = 1_dec;
        spec.fee_rate_maker    = 0.001_dec;
        spec.fee_rate_taker    = 0.002_dec;
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
    coro::sync_await(order.next());
    while (order.any_fill()) order.read_fill();
}

// Consume all updates until the order reaches a terminal state.
// Only call this after an on_event() that is expected to close the order.
static void drain_until_done(Order &order) {
    while (order.any_fill()) order.read_fill();
    bool cont = coro::sync_await(order.next());
    while (cont) {
        while (order.any_fill()) order.read_fill();
        cont = coro::sync_await(order.next());
    }
}

static void test_limit_buy_fills_on_quote() {
    OrderFixture fx;

    Order order = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 100_dec},
        "buy_test");
    drain_status(order);
    CHECK(order.get_status() == OrderStatus::open);

    // Unfavorable quote (ask=101 > limit=100) — must NOT fill
    fx.exchange->on_event("BTCUSD",
        Quote{.bid=99_dec, .bid_size=10_dec, .ask=101_dec, .ask_size=10_dec, .time=fx.t0});
    CHECK(order.get_status() == OrderStatus::open);
    CHECK(!order.any_fill());

    // Favorable quote (ask=99 < limit=100) — must fill at limit_price
    fx.exchange->on_event("BTCUSD",
        Quote{.bid=98_dec, .bid_size=10_dec, .ask=99_dec, .ask_size=10_dec, .time=fx.t0});

    auto fill = order.read_fill();
    CHECK(fill.has_value());
    CHECK(fill->price == 100_dec);
    CHECK(fill->amount == 1_dec);
    CHECK(fill->side == Side::buy);

    coro::sync_await(order.next());
    CHECK(order.get_status() == OrderStatus::filled);
}

static void test_limit_sell_fills_on_quote() {
    OrderFixture fx;

    Order order = fx.instrument->place_order(
        OrderRequest{.side = Side::sell, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 102_dec},
        "sell_test");
    drain_status(order);
    CHECK(order.get_status() == OrderStatus::open);

    // Unfavorable quote (bid=100 < limit=102) — must NOT fill
    fx.exchange->on_event("BTCUSD",
        Quote{.bid=100_dec, .bid_size=10_dec, .ask=103_dec, .ask_size=10_dec, .time=fx.t0});
    CHECK(order.get_status() == OrderStatus::open);
    CHECK(!order.any_fill());

    // Favorable quote (bid=103 > limit=102) — must fill at limit_price
    fx.exchange->on_event("BTCUSD",
        Quote{.bid=103_dec, .bid_size=10_dec, .ask=105_dec, .ask_size=10_dec, .time=fx.t0});

    auto fill = order.read_fill();
    CHECK(fill.has_value());
    CHECK(fill->price == 102_dec);
    CHECK(fill->amount == 1_dec);
    CHECK(fill->side == Side::sell);

    coro::sync_await(order.next());
    CHECK(order.get_status() == OrderStatus::filled);
}

static void test_limit_buy_fills_on_trade() {
    OrderFixture fx;

    Order order = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 100_dec},
        "buy_trade_test");
    drain_status(order);
    CHECK(order.get_status() == OrderStatus::open);

    // Trade at price=99 (below limit=100) with size=2 — must fill at limit_price,
    // capped at leave_quant=1 (verifies std::min fix, not std::max).
    fx.exchange->on_event("BTCUSD",
        Trade{.price=99_dec, .size=2_dec, .time=fx.t0});

    auto fill = order.read_fill();
    CHECK(fill.has_value());
    CHECK(fill->price == 100_dec);
    CHECK(fill->amount == 1_dec);   // must be 1, not 2
    CHECK(fill->side == Side::buy);

    coro::sync_await(order.next());
    CHECK(order.get_status() == OrderStatus::filled);
}

static void test_market_buy_fills_immediately() {
    OrderFixture fx;
    fx.exchange->set_slippage(0.0);  // disable slippage so fill price == ask exactly

    Order order = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::market, .quantity = 1_dec},
        "mkt_buy_test");
    drain_status(order);
    CHECK(order.get_status() == OrderStatus::open);

    // Feed a quote — market order should fill immediately on ask
    fx.exchange->on_event("BTCUSD",
        Quote{.bid=99_dec, .bid_size=10_dec, .ask=100_dec, .ask_size=10_dec, .time=fx.t0});

    auto fill = order.read_fill();
    CHECK(fill.has_value());
    CHECK(fill->price == 100_dec);   // ask price (slippage=0)
    CHECK(fill->amount == 1_dec);
    CHECK(fill->side == Side::buy);

    coro::sync_await(order.next());
    CHECK(order.get_status() == OrderStatus::filled);
}

static void test_cancel_order() {
    OrderFixture fx;

    Order order = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 100_dec},
        "cancel_test");
    drain_status(order);
    CHECK(order.get_status() == OrderStatus::open);

    order.cancel();
    drain_until_done(order);
    CHECK(order.get_status() == OrderStatus::canceled);
}

static void test_replace_order() {
    OrderFixture fx;

    // Place original limit buy @ 100
    Order original = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 100_dec},
        "orig_test");
    drain_status(original);
    CHECK(original.get_status() == OrderStatus::open);

    // Replace with lower limit @ 98
    Order replacement = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 98_dec},
        original,
        "replace_test");
    drain_status(replacement);
    // original should now be replaced
    drain_until_done(original);
    CHECK(original.get_status() == OrderStatus::replaced);
    CHECK(replacement.get_status() == OrderStatus::open);

    // Unfavorable quote (ask=99 > limit=98) — must NOT fill
    fx.exchange->on_event("BTCUSD",
        Quote{.bid=97_dec, .bid_size=10_dec, .ask=99_dec, .ask_size=10_dec, .time=fx.t0});
    CHECK(replacement.get_status() == OrderStatus::open);
    CHECK(!replacement.any_fill());

    // Favorable quote (ask=97 < limit=98) — must fill at limit_price
    fx.exchange->on_event("BTCUSD",
        Quote{.bid=95_dec, .bid_size=10_dec, .ask=97_dec, .ask_size=10_dec, .time=fx.t0});

    auto fill = replacement.read_fill();
    CHECK(fill.has_value());
    CHECK(fill->price == 98_dec);
    CHECK(fill->amount == 1_dec);
    CHECK(fill->side == Side::buy);

    coro::sync_await(replacement.next());
    CHECK(replacement.get_status() == OrderStatus::filled);
}

static void test_order_next_coroutine() {
    OrderFixture fx;

    Order order = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 100_dec},
        "coro_test");
    drain_status(order);
    CHECK(order.get_status() == OrderStatus::open);

    // Shared variables for communication with the coroutine
    bool coro_got_event = false;
    bool coro_ran = false;

    // Define and launch a StrategyFragment coroutine that co_awaits next()
    auto launch_coro = [&]() -> StrategyFragment {
        bool result = co_await order.next();
        coro_got_event = result;
        coro_ran = true;
    };

    StrategyFragment frag = launch_coro();
    // Enqueue the coroutine on the executor and run until it suspends
    fx.executor->run(std::move(frag));
    fx.executor->flush_queue();

    // Coroutine should have suspended waiting for an update — not yet done
    CHECK(!coro_ran);

    // Push a fill update directly into the order
    Fill fill{
        .key        = {1,1},
        .id         = "f1",
        .order_name = "coro_test",
        .time       = fx.t0,
        .contract   = {},
        .side       = Side::buy,
        .reason     = ExecutionReason::strategy_order,
        .amount     = 1_dec,
        .price      = 100_dec,
        .fees       = Decimal(0),
        .fee_rate   = Decimal(1),
    };
    order.update_order(Order::Update{std::move(fill)});

    // Pump the executor so the coroutine resumes
    fx.executor->flush_queue();

    // Coroutine should now have run to completion
    CHECK(coro_ran);
    CHECK(coro_got_event);
}

int main() {
    test_limit_buy_fills_on_quote();
    test_limit_sell_fills_on_quote();
    test_limit_buy_fills_on_trade();
    test_market_buy_fills_immediately();
    test_cancel_order();
    test_replace_order();
    test_order_next_coroutine();
    return 0;
}
