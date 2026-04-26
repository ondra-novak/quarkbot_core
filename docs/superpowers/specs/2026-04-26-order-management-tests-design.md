# Order Management Tests — Design

**Date:** 2026-04-26  
**Scope:** Basic happy-path coverage for order lifecycle + three bug fixes discovered during design

---

## Bug Fixes

### Bug 1 — `any_fill()` inverted predicate (`src/ifc/order.hpp`)

`any_fill()` currently returns `true` when the queue is empty or the front element is an `OrderStatusUpdate`, which is the opposite of its intended meaning. `read_fill()` calls it as a guard before calling `std::get<Fill>(front())`, so the current code produces UB/crash when a fill IS present and returns `nullopt` when it should not.

**Fix:**
```cpp
// before
return _state->updates.empty() || std::holds_alternative<OrderStatusUpdate>(_state->updates.front());

// after
return !_state->updates.empty() && std::holds_alternative<Fill>(_state->updates.front());
```

### Bug 3 — Inverted fill condition for limit orders in Quote matching (`src/impl/simexecutor.cpp`)

The condition `sgn(dp) * sid < 0` is correct for **stop** and **alert** orders (trigger when price crosses *past* the level in the favorable direction), but for **limit orders** the semantics are opposite: a limit order should fill when the market price *reaches or improves past* the limit price.

Correct semantics:
- **Limit buy**: fills when `ask ≤ limit_price` → `dp = limit_price − ask ≥ 0` → condition: `sgn(dp) * sid > 0`
- **Limit sell**: fills when `bid ≥ limit_price` → `dp = limit_price − bid ≤ 0` → `sgn(dp) * sid = (−1)*(−1) > 0`

The current code has `< 0`, which inverts both cases. Additionally:
- `limit_post_only` rejection must use the same condition (reject when the order would fill as taker) — also needs flipping.
- Exact-price match (`dp == 0`): currently fills only if there's insufficient quoted size (`dq > 0`); with the infinite-liquidity assumption, it should fill the complete remaining quantity regardless.

**Fix (in `match_order(Quote)`):**
```cpp
// limit_post_only rejection — before: sgn(dp) * sid < 0, after: sgn(dp) * sid > 0
if (taker && sgn(dp) * sid > 0) { ... reject ... }

// limit/limit_ioc fill condition — before: sgn(dp) * sid < 0, after: sgn(dp) * sid > 0
// exact price (dp == 0): fill leave_quant instead of dq, remove dq > 0 guard
if (dp == 0) {
    create_fill(order, p, leave_quant, quote.time, taker);
    break;
}
else if (sgn(dp) * sid > 0) {
    create_fill(order, params.limit_price, leave_quant, quote.time, taker);
    break;
}
```

Stop and alert conditions (`sgn(dp) * sid < 0`) are **not changed** — they are correct as-is.

### Bug 2 — `std::max` overfills on Trade match (`src/impl/simexecutor.cpp:213`)

When a `Trade` event triggers a limit order, the fill quantity is `std::max(leave_quant, trade.size)`. If `trade.size > leave_quant` the order is filled for more than its total quantity.

**Fix:**
```cpp
// before
create_fill(order, params.limit_price, std::max(leave_quant, trade.size), trade.time, false);

// after
create_fill(order, params.limit_price, std::min(leave_quant, trade.size), trade.time, false);
```

---

## Test File

**Path:** `src/tests/order_test.cpp`  
**Registration:** added to `BASIC_TESTS` list in `src/tests/CMakeLists.txt`  
**Style:** matches existing tests — standalone `main()`, `check.h` macros, no external test framework

---

## Common Fixture

A plain `struct OrderFixture` instantiated at the top of each test function:

```cpp
struct OrderFixture {
    std::shared_ptr<BacktestExecutor>     executor;
    std::shared_ptr<SimExchange>          exchange;
    PAccount                              account;
    PTradableInstrument                   instrument;
    std::chrono::system_clock::time_point t0;
};
```

**Setup:**
- `BacktestExecutor` created and attached to thread (`attach_to_thread()`)
- `SimExchange` with one spot BTCUSD instrument (lot size 0.00001, price increment 0.01, no leverage)
- Account with 10 000 USD wallet
- `t0 = std::chrono::system_clock::now()`

---

## Helpers

### `drain_status(Order &)` — consume one pending status update

Called immediately after `place_order()` to flush the queued `open` status update into `order.get_status()`. Safe because `accept_order()` always queues exactly one update before returning.

```cpp
void drain_status(Order &order) {
    coro::sync_await(order.next_event());  // immediately ready — update already in queue
    while (order.any_fill()) order.read_fill();
}
```

### `drain_until_done(Order &)` — consume all updates until terminal state

Called after feeding a matching market event, when the order is expected to reach a terminal state (`filled`, `canceled`, `replaced`). Safe because `sync_await` is immediately ready as long as there are updates in the queue, and the loop exits when `next_event()` returns `false` (terminal state, empty queue).

```cpp
void drain_until_done(Order &order) {
    while (order.any_fill()) order.read_fill();
    bool cont = coro::sync_await(order.next_event());
    while (cont) {
        while (order.any_fill()) order.read_fill();
        cont = coro::sync_await(order.next_event());
    }
}
```

**Important:** `sync_await(order.next_event())` must never be called when the queue is empty and the order is still open — it would block waiting for an update that only arrives via a coroutine resume. These helpers are only safe in the described calling contexts.

---

## Test Scenarios

### 1. `test_limit_buy_fills_on_quote`

Place limit buy qty=1 @ 100. Feed `Quote{bid=98, ask=99}` (ask < limit).  
Expected: `status == filled`, `filled == 1`, one fill with `price == 100`, `side == buy`.  
Also verify: feeding `Quote{bid=99, ask=101}` (ask > limit) does NOT fill the order.

### 2. `test_limit_sell_fills_on_quote`

Place limit sell qty=1 @ 102. Feed `Quote{bid=103, ask=105}` (bid > limit).  
Expected: `status == filled`, fill `price == 102`, `side == sell`.  
Also verify: feeding `Quote{bid=100, ask=103}` (bid < limit) does NOT fill the order.

### 3. `test_limit_buy_fills_on_trade`

Place limit buy qty=1 @ 100. Feed `Trade{price=99, size=2}`.  
Expected: `status == filled`, fill quantity == 1 (not 2 — verifies the `std::min` fix).

### 4. `test_market_buy_fills_immediately`

Place market buy qty=1. Feed a quote with sufficient ask size.  
Expected: `status == filled` on the same quote; fill price close to ask (slippage applied).

### 5. `test_cancel_order`

Place limit buy @ 100 (no matching quote yet). Call `order.cancel()`. Drain.  
Expected: `status == canceled`.

### 6. `test_replace_order`

Place limit buy @ 100. Replace with limit buy @ 98 (same side/type). Feed `Quote{bid=96, ask=97}` (ask < 98).  
Expected: original order `status == replaced`; new order `status == filled`.

### 7. `test_order_next_event_coroutine`

Single coroutine test. A `StrategyFragment` coroutine suspends on `co_await order.next_event()`. A fill update is pushed synchronously from the test thread via `order.update_order(Fill{...})` after `BacktestExecutor::resume()` is called to run pending coroutines.  
Expected: coroutine wakes up and observes the fill.

---

## Out of Scope (deferred to advanced test files)

- Insufficient funds / margin rejection
- `limit_post_only` taker rejection
- `limit_ioc` partial fill then cancel
- Stop / stop-limit / OCO triggering
- Alert order type
- Order serialization / restore
