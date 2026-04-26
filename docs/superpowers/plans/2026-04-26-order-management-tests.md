# Order Management Tests Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix four bugs in the order management system and write `src/tests/order_test.cpp` covering the basic order lifecycle.

**Architecture:** Tests use `SimExchange` + `BacktestExecutor` directly (no `Backtest` wrapper). `attach_to_thread()` makes the test thread the execution worker, enabling `coro::sync_await` on immediately-ready awaitables. All scenarios are driven by `exchange->on_event()` calls with crafted `Quote`/`Trade` values.

**Tech Stack:** C++23, BasicCoro (`coro::sync_await`, `coro::prepared_coro`), `check.h` macros, `SimExchange`, `BacktestExecutor`, `InstrumentSpec`

---

## File Map

| Action | Path |
|--------|------|
| Modify | `src/ifc/order.hpp` — fix Bug 4 (State deadlock) and Bug 1 (any_fill inverted) |
| Modify | `src/impl/simexecutor.cpp` — fix Bug 3 (limit fill condition) and Bug 2 (std::max→std::min) |
| Create | `src/tests/order_test.cpp` — all seven tests |
| Modify | `src/tests/CMakeLists.txt` — register `order_test.cpp` |

---

## Task 1: Register test file and create compile-only skeleton

**Files:**
- Modify: `src/tests/CMakeLists.txt`
- Create: `src/tests/order_test.cpp`

- [ ] **Step 1: Add `order_test.cpp` to `BASIC_TESTS` in `src/tests/CMakeLists.txt`**

```cmake
set(BASIC_TESTS
    compile_test.cpp
    decimal_test.cpp
    pubsub.cpp
    mem_storage_test.cpp
    instrument_spec_test.cpp
    tardis_source_test.cpp
    merged_source_test.cpp
    order_test.cpp
)
```

- [ ] **Step 2: Create `src/tests/order_test.cpp` with skeleton**

```cpp
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

int main() {
    return 0;
}
```

- [ ] **Step 3: Build to verify it compiles**

```bash
make -C build tests_order_test
```

Expected: compiles with no errors.

- [ ] **Step 4: Commit**

```bash
git add src/tests/CMakeLists.txt src/tests/order_test.cpp
git commit -m "test: add order_test.cpp skeleton and register in CMakeLists"
```

---

## Task 2: Fix Bug 4 — Double-lock deadlock in `Order::State`

**Files:**
- Modify: `src/ifc/order.hpp:229-246`

**Root cause:** `State::update()` holds `std::scoped_lock(mx)` and then calls `updates.push()`, which internally also acquires `mx` (`std::lock_guard` in `DoubleBufferQueue::push()`). Locking a non-recursive `std::mutex` twice from the same thread is undefined behavior (deadlock on Linux/glibc). `State::pull()` has the same problem: it holds `mx` and calls `flush_state()` → `updates.front()` → buffer-swap code which also acquires `mx`.

**Fix:** In `update()`, remove the outer lock and the `flush_state()` call — `push()` is already thread-safe on its own, and `flush_state()` is a consumer-side operation (called by `pull()` on the consumer thread). In `pull()`, release the lock before calling `flush_state()`.

- [ ] **Step 1: Fix `State::update()` in `src/ifc/order.hpp`**

Find (around line 229):
```cpp
        void update(Update &&u) {
            std::scoped_lock _(mx);
            updates.push(std::move(u));
            if (awaiting) {
                flush_state();
                awaiting(true);                
            }
        }
```

Replace with:
```cpp
        void update(Update &&u) {
            updates.push(std::move(u));
            if (awaiting) {
                awaiting(true);
            }
        }
```

- [ ] **Step 2: Fix `State::pull()` in `src/ifc/order.hpp`**

Find (around line 238):
```cpp
        std::optional<bool> pull() {
            std::scoped_lock _(mx);
            if (updates.empty()) {
                if (is_done_status(status)) return false;
                return {};
            }
            flush_state();
            return true;
        }
```

Replace with:
```cpp
        std::optional<bool> pull() {
            {
                std::scoped_lock _(mx);
                if (updates.empty()) {
                    if (is_done_status(status)) return false;
                    return {};
                }
            }
            flush_state();
            return true;
        }
```

- [ ] **Step 3: Build**

```bash
make -C build tests_order_test
```

Expected: compiles with no errors.

- [ ] **Step 4: Commit**

```bash
git add src/ifc/order.hpp
git commit -m "fix: remove double-lock deadlock in Order::State update() and pull()"
```

---

## Task 3: Fix Bug 1 (`any_fill()` inverted) + add `OrderFixture` and drain helpers

**Files:**
- Modify: `src/ifc/order.hpp:285-287`
- Modify: `src/tests/order_test.cpp`

**Root cause (Bug 1):** `any_fill()` returns `true` when the queue is `empty()` OR the front is an `OrderStatusUpdate` — the exact opposite of its intent. `read_fill()` calls `any_fill()` as a guard before `std::get<Fill>(front())`, so the current code crashes when a fill IS present and silently returns `nullopt` when it is not.

- [ ] **Step 1: Fix `any_fill()` in `src/ifc/order.hpp`**

Find (around line 285):
```cpp
    bool any_fill() const {
        return _state->updates.empty() || std::holds_alternative<OrderStatusUpdate>(_state->updates.front());        
    }
```

Replace with:
```cpp
    bool any_fill() const {
        return !_state->updates.empty() && std::holds_alternative<Fill>(_state->updates.front());
    }
```

- [ ] **Step 2: Add `OrderFixture`, `drain_status`, and `drain_until_done` to `src/tests/order_test.cpp`**

Replace the current `#include` block and `main()` with:

```cpp
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
```

- [ ] **Step 3: Build**

```bash
make -C build tests_order_test
```

Expected: compiles with no errors.

- [ ] **Step 4: Commit**

```bash
git add src/ifc/order.hpp src/tests/order_test.cpp
git commit -m "fix: invert any_fill() predicate; add OrderFixture and drain helpers to order_test"
```

---

## Task 4: Fix Bug 3 (limit fill condition inverted) + Tests 1 and 2

**Files:**
- Modify: `src/impl/simexecutor.cpp:158-178`
- Modify: `src/tests/order_test.cpp`

**Root cause (Bug 3):** In `match_order(ActiveOrder, Quote, taker)`, the fill condition `sgn(dp) * sid < 0` is correct for stop/alert orders (trigger when price crosses *past* the level), but is inverted for limit orders. For a **limit buy** `dp = limit − ask`; the order fills when `ask ≤ limit`, i.e. `dp ≥ 0`, i.e. `sgn(dp) * sid > 0`. The current `< 0` condition fills the buy when `ask > limit` — wrong. The exact-price case (`dp == 0`) also has a bug: it fills only `dq` (quantity beyond quoted size) instead of filling the entire remaining quantity (infinite-liquidity assumption). `limit_post_only` rejection needs the same flip.

- [ ] **Step 1: Write `test_limit_buy_fills_on_quote` in `src/tests/order_test.cpp`**

Replace `// --- placeholder tests ---` and the `main()`:

```cpp
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

    coro::sync_await(order.next_event());
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

    coro::sync_await(order.next_event());
    CHECK(order.get_status() == OrderStatus::filled);
}

int main() {
    test_limit_buy_fills_on_quote();
    test_limit_sell_fills_on_quote();
    return 0;
}
```

- [ ] **Step 2: Build and run to confirm FAILURE (tests hang or report wrong status before the fix)**

```bash
make -C build tests_order_test && ./build/tests/tests_order_test
```

Expected: tests fail — the order either does not fill on the favorable quote or fills on the unfavorable one.

- [ ] **Step 3: Fix Bug 3 in `src/impl/simexecutor.cpp`**

Find the `limit_post_only` / `limit_ioc` / `limit` block (around line 158):
```cpp
            case OrderType::limit_post_only:
                if (taker && sgn(dp) * sid < 0) {
                    set_order_status(order.ord, {OrderStatus::rejected, OrderRejectionReason::post_only_taker});
                    return true;
                }
                [[fallthrough]];
            case OrderType::limit_ioc:
            case OrderType::limit:
                if (dp == 0 && dq > 0) {
                    create_fill(order,p, dq, quote.time, taker);                            
                    break;
                }
                else if (sgn(dp) * sid < 0) {
                    create_fill(order, params.limit_price, leave_quant, quote.time, taker);
                    break;
                }                        
                if (params.type == OrderType::limit_ioc) {
                    set_order_status(order.ord, {OrderStatus::filled});
                    return true;
                }
                return false;     
```

Replace with:
```cpp
            case OrderType::limit_post_only:
                if (taker && sgn(dp) * sid > 0) {
                    set_order_status(order.ord, {OrderStatus::rejected, OrderRejectionReason::post_only_taker});
                    return true;
                }
                [[fallthrough]];
            case OrderType::limit_ioc:
            case OrderType::limit:
                if (dp == 0) {
                    create_fill(order, p, leave_quant, quote.time, taker);
                    break;
                }
                else if (sgn(dp) * sid > 0) {
                    create_fill(order, params.limit_price, leave_quant, quote.time, taker);
                    break;
                }
                if (params.type == OrderType::limit_ioc) {
                    set_order_status(order.ord, {OrderStatus::filled});
                    return true;
                }
                return false;
```

(Stop/alert conditions `sgn(dp) * sid < 0` are NOT changed — they remain correct.)

- [ ] **Step 4: Build and run**

```bash
make -C build tests_order_test && ./build/tests/tests_order_test
```

Expected output (both tests pass):
```
Passed: order.get_status() == OrderStatus::open
Passed: !order.any_fill()
Passed: fill.has_value()
Passed: fill->price == 100_dec
Passed: fill->amount == 1_dec
Passed: fill->side == Side::buy
Passed: order.get_status() == OrderStatus::filled
Passed: order.get_status() == OrderStatus::open
Passed: !order.any_fill()
Passed: fill.has_value()
Passed: fill->price == 102_dec
Passed: fill->amount == 1_dec
Passed: fill->side == Side::sell
Passed: order.get_status() == OrderStatus::filled
```

- [ ] **Step 5: Commit**

```bash
git add src/impl/simexecutor.cpp src/tests/order_test.cpp
git commit -m "fix: invert limit order fill condition in match_order(Quote); add limit buy/sell tests"
```

---

## Task 5: Fix Bug 2 (`std::max` → `std::min` in Trade fill) + Test 3

**Files:**
- Modify: `src/impl/simexecutor.cpp:213`
- Modify: `src/tests/order_test.cpp`

**Root cause (Bug 2):** When a `Trade` event matches a limit order, the fill quantity is `std::max(leave_quant, trade.size)`. If `trade.size > leave_quant`, the order is overfilled beyond its total quantity. Should be `std::min`.

- [ ] **Step 1: Add `test_limit_buy_fills_on_trade` to `src/tests/order_test.cpp`**

Add before `int main()`:
```cpp
static void test_limit_buy_fills_on_trade() {
    OrderFixture fx;

    // Place limit buy qty=1 @ 100
    Order order = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 100_dec},
        "trade_test");
    drain_status(order);

    // Trade at price=99 (≤ limit=100) with size=2 (larger than order qty)
    // After fix: fill should be min(1, 2) = 1, not max(1, 2) = 2
    fx.exchange->on_event("BTCUSD",
        Trade{.price=99_dec, .size=2_dec, .time=fx.t0});

    auto fill = order.read_fill();
    CHECK(fill.has_value());
    CHECK(fill->amount == 1_dec);   // must be 1, not 2

    coro::sync_await(order.next_event());
    CHECK(order.get_status() == OrderStatus::filled);
}
```

Update `main()`:
```cpp
int main() {
    test_limit_buy_fills_on_quote();
    test_limit_sell_fills_on_quote();
    test_limit_buy_fills_on_trade();
    return 0;
}
```

- [ ] **Step 2: Build and run to confirm FAILURE**

```bash
make -C build tests_order_test && ./build/tests/tests_order_test
```

Expected: `fill->amount == 1_dec` fails (fill amount is 2 before fix).

- [ ] **Step 3: Fix Bug 2 in `src/impl/simexecutor.cpp`**

Find (around line 213):
```cpp
                create_fill(order, params.limit_price, std::max(leave_quant, trade.size), trade.time, false);
```

Replace with:
```cpp
                create_fill(order, params.limit_price, std::min(leave_quant, trade.size), trade.time, false);
```

- [ ] **Step 4: Build and run**

```bash
make -C build tests_order_test && ./build/tests/tests_order_test
```

Expected: all three tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/impl/simexecutor.cpp src/tests/order_test.cpp
git commit -m "fix: use std::min for trade fill quantity to prevent overfill; add trade fill test"
```

---

## Task 6: Test 4 — market buy fills immediately

**Files:**
- Modify: `src/tests/order_test.cpp`

A market order must fill on the very first quote with sufficient ask size. With `_slippage = 0`, the fill price equals `ask`. If ask size ≥ order qty (no `dq > 0` path), slippage applies; if ask size < order qty, exact ask price is used for the over-sized partial fill first (the code in `match_order` handles this in the `market` branch).

- [ ] **Step 1: Add `test_market_buy_fills_immediately` to `src/tests/order_test.cpp`**

Add before `int main()`:
```cpp
static void test_market_buy_fills_immediately() {
    OrderFixture fx;

    Order order = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::market,
                     .quantity = 1_dec},
        "market_test");
    drain_status(order);
    CHECK(order.get_status() == OrderStatus::open);

    // Feed a quote with ask=100, ask_size=10 (more than order qty=1)
    // Market order fills immediately — fill price near ask (slippage default 0)
    fx.exchange->on_event("BTCUSD",
        Quote{.bid=99_dec, .bid_size=10_dec, .ask=100_dec, .ask_size=10_dec, .time=fx.t0});

    auto fill = order.read_fill();
    CHECK(fill.has_value());
    CHECK(fill->side == Side::buy);
    CHECK(fill->amount == 1_dec);

    coro::sync_await(order.next_event());
    CHECK(order.get_status() == OrderStatus::filled);
}
```

Update `main()`:
```cpp
int main() {
    test_limit_buy_fills_on_quote();
    test_limit_sell_fills_on_quote();
    test_limit_buy_fills_on_trade();
    test_market_buy_fills_immediately();
    return 0;
}
```

- [ ] **Step 2: Build and run**

```bash
make -C build tests_order_test && ./build/tests/tests_order_test
```

Expected: all four tests pass.

- [ ] **Step 3: Commit**

```bash
git add src/tests/order_test.cpp
git commit -m "test: add market buy fills immediately test"
```

---

## Task 7: Test 5 — cancel order

**Files:**
- Modify: `src/tests/order_test.cpp`

- [ ] **Step 1: Add `test_cancel_order` to `src/tests/order_test.cpp`**

`ITradableInstrument::cancel_order(Order)` routes to `SimExchange::cancel_order()` → `SimExecutor::cancel_order()`.

Add before `int main()`:
```cpp
static void test_cancel_order() {
    OrderFixture fx;

    Order order = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 100_dec},
        "cancel_test");
    drain_status(order);
    CHECK(order.get_status() == OrderStatus::open);

    fx.instrument->cancel_order(order);

    drain_until_done(order);
    CHECK(order.get_status() == OrderStatus::canceled);
}
```

Update `main()`:
```cpp
int main() {
    test_limit_buy_fills_on_quote();
    test_limit_sell_fills_on_quote();
    test_limit_buy_fills_on_trade();
    test_market_buy_fills_immediately();
    test_cancel_order();
    return 0;
}
```

- [ ] **Step 2: Build and run**

```bash
make -C build tests_order_test && ./build/tests/tests_order_test
```

Expected: all five tests pass.

- [ ] **Step 3: Commit**

```bash
git add src/tests/order_test.cpp
git commit -m "test: add cancel order test"
```

---

## Task 8: Test 6 — replace order

**Files:**
- Modify: `src/tests/order_test.cpp`

`ITradableInstrument::place_order(params, order_to_replace, name)` replaces an existing order. The original order receives `OrderStatus::replaced`. The new order must then fill when a matching quote arrives.

- [ ] **Step 1: Add `test_replace_order` to `src/tests/order_test.cpp`**

Add before `int main()`:
```cpp
static void test_replace_order() {
    OrderFixture fx;

    // Place limit buy @ 100 (won't fill yet)
    Order original = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 100_dec},
        "original");
    drain_status(original);
    CHECK(original.get_status() == OrderStatus::open);

    // Replace with limit buy @ 98 (same side and type required)
    Order replacement = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 98_dec},
        original, "replacement");

    // Drain both updates: original gets "replaced", replacement gets "open"
    drain_status(replacement);

    coro::sync_await(original.next_event());
    CHECK(original.get_status() == OrderStatus::replaced);

    // Feed a quote that matches replacement (ask=97 < limit=98)
    // Original is no longer in active orders so only replacement fills
    fx.exchange->on_event("BTCUSD",
        Quote{.bid=96_dec, .bid_size=10_dec, .ask=97_dec, .ask_size=10_dec, .time=fx.t0});

    auto fill = replacement.read_fill();
    CHECK(fill.has_value());
    CHECK(fill->price == 98_dec);
    CHECK(fill->amount == 1_dec);

    coro::sync_await(replacement.next_event());
    CHECK(replacement.get_status() == OrderStatus::filled);
}
```

Update `main()`:
```cpp
int main() {
    test_limit_buy_fills_on_quote();
    test_limit_sell_fills_on_quote();
    test_limit_buy_fills_on_trade();
    test_market_buy_fills_immediately();
    test_cancel_order();
    test_replace_order();
    return 0;
}
```

- [ ] **Step 2: Build and run**

```bash
make -C build tests_order_test && ./build/tests/tests_order_test
```

Expected: all six tests pass.

- [ ] **Step 3: Commit**

```bash
git add src/tests/order_test.cpp
git commit -m "test: add replace order test"
```

---

## Task 9: Test 7 — coroutine `next_event` wakeup

**Files:**
- Modify: `src/tests/order_test.cpp`

This test verifies that `Order::next_event()` correctly suspends a coroutine and resumes it when `update_order()` pushes a fill. It uses `StrategyFragment` (pool-allocated coroutine promise) and `BacktestExecutor`'s dispatch queue.

**Sequence:**
1. Place limit buy; drain the initial `open` status so the queue is empty.
2. Start a `StrategyFragment` that `co_await`s `order.next_event()` — it will suspend because the queue is empty.
3. `flush_queue()` runs the coroutine up to the suspension point.
4. Push a synthetic `Fill` directly via `order.update_order(fill)` — this sets `awaiting` on the coroutine handle via `BacktestExecutor::resume()`.
5. `flush_queue()` resumes the coroutine.
6. Verify the coroutine observed the fill.

- [ ] **Step 1: Add `test_order_next_event_coroutine` to `src/tests/order_test.cpp`**

Add before `int main()`:
```cpp
static void test_order_next_event_coroutine() {
    OrderFixture fx;
    bool fill_received = false;

    Order order = fx.instrument->place_order(
        OrderRequest{.side = Side::buy, .type = OrderType::limit,
                     .quantity = 1_dec, .limit_price = 100_dec},
        "coro_test");

    // Drain the "open" status synchronously — immediately ready, no blocking
    coro::sync_await(order.next_event());

    // Define coroutine: suspends waiting for next_event, then reads fill
    auto fragment = [&]() -> StrategyFragment {
        bool cont = co_await order.next_event();  // suspends: queue empty, not done
        if (cont && order.any_fill()) {
            order.read_fill();
            fill_received = true;
        }
        co_return;
    };

    // Queue the coroutine handle, then run until it suspends on next_event
    fx.executor->run(fragment());
    fx.executor->flush_queue();
    CHECK(!fill_received);  // not yet — suspended waiting

    // Push a synthetic fill — wakes the coroutine via BacktestExecutor's dispatch queue
    Fill f;
    f.side   = Side::buy;
    f.amount = 1_dec;
    f.price  = 100_dec;
    f.time   = fx.t0;
    order.update_order(std::move(f));

    // Resume the coroutine; it reads the fill and sets fill_received
    fx.executor->flush_queue();
    CHECK(fill_received);
}
```

Update `main()`:
```cpp
int main() {
    test_limit_buy_fills_on_quote();
    test_limit_sell_fills_on_quote();
    test_limit_buy_fills_on_trade();
    test_market_buy_fills_immediately();
    test_cancel_order();
    test_replace_order();
    test_order_next_event_coroutine();
    return 0;
}
```

- [ ] **Step 2: Build and run**

```bash
make -C build tests_order_test && ./build/tests/tests_order_test
```

Expected: all seven tests pass.

- [ ] **Step 3: Run via CTest**

```bash
ctest --test-dir build -R "tests/order_test" -V
```

Expected: `1/1 Test #N: tests/order_test.cpp ... Passed`.

- [ ] **Step 4: Commit**

```bash
git add src/tests/order_test.cpp
git commit -m "test: add coroutine next_event wakeup test; complete order_test suite"
```

---

## Summary

Four bugs fixed:

| Bug | File | Fix |
|-----|------|-----|
| Bug 4 — State deadlock | `src/ifc/order.hpp` | Remove outer lock in `update()`; move `flush_state()` outside lock in `pull()` |
| Bug 1 — `any_fill()` inverted | `src/ifc/order.hpp` | `empty() \|\| holds<StatusUpdate>` → `!empty() && holds<Fill>` |
| Bug 3 — limit fill condition inverted | `src/impl/simexecutor.cpp` | `sgn(dp)*sid < 0` → `> 0` for limit/limit_post_only; exact-price fills `leave_quant` not `dq` |
| Bug 2 — overfill on Trade | `src/impl/simexecutor.cpp` | `std::max` → `std::min` in trade fill quantity |

Seven tests added in `src/tests/order_test.cpp`:
1. `test_limit_buy_fills_on_quote`
2. `test_limit_sell_fills_on_quote`
3. `test_limit_buy_fills_on_trade`
4. `test_market_buy_fills_immediately`
5. `test_cancel_order`
6. `test_replace_order`
7. `test_order_next_event_coroutine`
