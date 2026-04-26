# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# Configure (only needed once)
cmake -B build

# Build everything
make -C build -j$(nproc)

# Run all tests
ctest --test-dir build

# Run a single test binary
./build/tests/tests_decimal_test
```

Test binaries land in `build/tests/`. The build outputs libraries to `build/lib/` and executables to `build/bin/`.

The project requires **C++23**, **LevelDB** (`libleveldb-dev`), and **ZLIB**. The `BasicCoro` coroutine library is fetched automatically via CMake FetchContent.

To customise CMake flags (e.g. switch compiler), edit `default_build_profile.conf`.

## Architecture

Everything lives in the `quarkbot` namespace. The codebase is split into three layers:

### Interfaces (`src/ifc/`)

Pure-virtual contracts that exchange adapters and simulators must implement:

- **`IExchange`** — top-level factory: creates accounts and lists instruments.
- **`IMarketInstrument`** — read-only market data access; subscribe to event streams.
- **`ITradableInstrument`** — adds order placement/cancellation on top of a market instrument; created from `IMarketInstrument::create_tradable_instrument(account)`.
- **`IAccount`** — holds wallet/margin state.
- **`IExecutionWorker`** — runs coroutines; one per thread. `IExecutionWorker::current()` returns the worker for the calling thread.
- **`IScheduler`** — `now()`, `sleep_until()`, `sleep_for()`. In backtest the time is simulated.
- **`IStorage`** / **`IStorageTransaction`** — key-value store with optional per-key revision history. Backed by LevelDB in production, `MemStorage` in tests.
- **`IBacktestDataSource`** — yields `Event{time, instrument, variant<Quote,Trade,OrderBookIncrement>}` one at a time.

### Strategy API (`src/ifc/context.hpp`, `src/ifc/order.hpp`, etc.)

Strategies are written as C++20 coroutines returning **`StrategyFragment`** (a specialisation of `coro::coroutine<void>` with a pool-allocated promise).

A strategy receives a **`StrategyContext`** containing:
```cpp
std::vector<PTradableInstrument> instruments;
PScheduler scheduler;
PStorage storage;
PExecutionWorker exec_worker;
StrategyMode mode;  // live_trading | backtest | paper_trading
std::function<awaitable<coro::void_type>()> stop_signal;
```

**Placing orders** — call `instrument->place_order(params, name)`, which returns an `Order`. Await order events:
```cpp
while (co_await order.next_event()) { /* check order.get_status() / order.read_fill() */ }
```

**Subscribing to market data** — call `instrument->subscribe<T>()` where `T` is a `StreamType`:
```cpp
EventStream<Quote> qs = instr->subscribe<Quote>();
Quote q;
while (co_await qs.next(q)) { /* use q */ }
```

Built-in stream types: `Quote`, `Trade`, `OrderBookIncrement`, `ClosedBar` / `ClosedBarInterval<seconds>`, `OrderBook<depth>`, `TradeCounter` (on `IMarketInstrument`); `ExternalFill`, `FundingEvent`, `OrderEvent` (on `ITradableInstrument`).

**Adding a new stream type**: derive from `MarketInstrumentStreamTypeItem` or `TradableInstrumentStreamTypeItem`, add `static constexpr Type type = "unique_string_id"` and a `view()` method returning a copy-assignable type.

### Implementations (`src/impl/`)

- **`SimExchange` / `SimInstrument` / `SimTradableInstrument` / `SimAccount` / `SimExecutor`** — full simulated exchange used in backtesting.
- **`BacktestExecutor`** — implements both `IExecutionWorker` and `IScheduler`; drives simulated time and the coroutine dispatch queue. The backtest loop feeds events from the data source, advances simulated time, and resumes waiting coroutines in order.
- **`Backtest`** — convenience wrapper that wires the above together; see `src/exec/print_events.cpp` for usage.
- **Data sources**: `MMBOT_backtest_datasource` (price-list CSV), `TardisTradesDataSource` / `TardisQuotesDataSource` (Tardis.dev CSV exports), `MergedDataSource` (merges multiple sources by time).
- **Streaming internals** (`src/impl/streaming/`): `LockFreePublisher<ViewType, N>` is the hot-path publisher; `PublisherManager` routes subscribe calls to the right publisher by stream type string.

### Utilities (`src/utils/`)

Header-only helpers: `Decimal` (fixed-point arithmetic), `double_buffer.hpp` (lock-aware double-buffer queue used inside `Order::State`), `pubsub.hpp`, `lockfree_queue.hpp`, `dispatcher.hpp`, `signals.hpp`, and technical analysis primitives in `src/ta/` (`EMA`, `BB_EMA`).

### Strategies (`src/strategies/`)

Each strategy is a shared library (or compiled unit) exposing a single factory function:
```cpp
StrategyFragment my_strategy(StrategyContext &context);
```

`print_events` is the reference/demo strategy. `trending` is a more complete example using `ClosedBarInterval` and limit orders.

## Key conventions

- `P<X>` type aliases are `shared_ptr<IX>` (e.g. `PAccount = shared_ptr<IAccount>`).
- `awaitable<T>` is `coro::awaitable<T>` from BasicCoro.
- `Order::next_event()` **must** be awaited from an execution worker thread; calling it elsewhere throws.
- `is_done_status(OrderStatus)` covers `filled`, `canceled`, `rejected`, `replaced`, `lost`.
- `Decimal` literals: `1000_dec`, `0.00001_dec`.
