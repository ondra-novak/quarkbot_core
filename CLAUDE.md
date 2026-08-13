# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Quarkbot Core is a C++23 trading interface library using coroutines for writing async trading strategies. It provides a full backtesting/simulation framework with no live exchange adapters (those are separate projects).

## Build Commands

```bash
# Configure (from repo root)
mkdir -p build && cd build
cmake ..

# Build all targets
cmake --build . -j$(nproc)

# Run all tests
ctest --test-dir build

# Run a single test binary (named test_<source-filename-without-.cpp>)
./build/tests/test_decimal_test
./build/tests/test_hub

# Enable optional components (off by default) and rebuild
cmake -DQUARKBOT_NETWORK=ON -DQUARKBOT_LEVELDB=ON ..

# Build with clang (edit default_build_profile.conf or pass directly)
cmake -DCMAKE_CXX_COMPILER=clang++ ..
```

Test binaries are in `build/tests/`, all other executables (strategy backtests) in `build/bin/`, libraries in `build/lib/`.

`QUARKBOT_TESTS` is ON by default when the project is built top-level, and it force-enables the network component so the full test suite can compile. `QUARKBOT_NETWORK` and `QUARKBOT_LEVELDB` are OFF by default because they pull in external deps (openssl, leveldb). The tardis, trth and algoseek components are always built; zlib is required unconditionally at the top level.

To add a compiler flag override, edit `default_build_profile.conf` (one `-DVariable=Value` per line).

## Architecture

The codebase splits into a **public header-only SDK** (`include/quarkbot/`) and a **compiled implementation library** (`src/quarkbot/`), consumed by strategies and executables:

```
Executables (src/exec/)              ← backtest drivers, link a strategy + impl + sdk
    └── Strategy libraries (src/strategies/)   ← reusable strategy logic
        └── Implementation (src/quarkbot/ → quarkbot::impl)  ← sim/backtest engines, network, storage, importers
            └── Public SDK (include/quarkbot/ → quarkbot::sdk)  ← interfaces, order/decimal, event views, TA, utils
```

Two CMake targets tie it together:

- `quarkbot::sdk` — INTERFACE (header-only) library exposing everything under `include/quarkbot/`.
- `quarkbot::impl` — static library built from `src/quarkbot/`; links `quarkbot::sdk` publicly.

### Public SDK (`include/quarkbot/`)

The installable API surface. Header-only; nearly everything a strategy needs is here.

**Abstract interfaces (`include/quarkbot/abstract/`)** — pure virtual contracts (`iexchange.hpp`, `iaccount.hpp`, `itradable_instrument.hpp`, `imarket_instrument.hpp`, `iexecution_worker.hpp`, `ieventstream.hpp`, `ipublisher.hpp`, `imessage_bus.hpp`, `backtest_data_source.hpp`, `orderdata.hpp`, `somodule_def.hpp`). Their `shared_ptr`-wrapped aliases and core typedefs live in `defs.hpp`:

- `PExchange` / `IExchange` — entry point; resolves instruments and accounts
- `PAccount` / `IAccount` — wallet and balance queries
- `PTradableInstrument` / `ITradableInstrument` — order placement and management
- `PMarketInstrument` / `IMarketInstrument` — market data subscriptions and metadata
- `PExecutionWorker` / `IExecutionWorker` — coroutine scheduler; `schedule()` transfers execution context
- `PBacktestDataSource` / `IBacktestDataSource` — historical data feed for backtesting
- `IEventStream<ViewType>` / `IPublisher<StreamTypeClass>` — typed event streaming

**Concrete value/support types (top level of `include/quarkbot/`)** — `order.hpp` (state machine tracking fills and status transitions), `decimal.hpp` (fixed-point financial arithmetic), `hub.hpp` (`Hub<T>` coroutine push/pop sync primitive), plus `account.hpp`, `exchange.hpp`, `market_instrument.hpp`, `tradable_instrument.hpp`, `event_stream.hpp`, `config.hpp`, `somodule.hpp`, `timer.hpp`, and coroutine helpers (`async.hpp`, `awaitable_stop.hpp`, `strategy_fragment.hpp`).

**Event view types (`include/quarkbot/stream/`)** — the payloads carried by event streams: `quote.hpp`, `trade.hpp`, `ticker.hpp`, `orderbook.hpp`, `closedbar.hpp`, `rangedbar.hpp`, `auction.hpp`, `funding_event.hpp`, `tradestat.hpp`, `periodic_snapshot.hpp`, `external_fill.hpp`.

**Technical analysis (`include/quarkbot/ta/`)** — `ema.hpp` (EMA) and `bb_ema.hpp` (Bollinger Bands over EMA).

**Utilities (`include/quarkbot/utils/`)** — `acb.hpp` (Average Cost Basis), `signals.hpp` / `signals_async.hpp`, `lockfree_queue.hpp` (SPSC/MPSC), `csv_reader.h`, `tagset.hpp`, `simple_ini.hpp`, `function_view.hpp`, `small_buffer.hpp`, `spin_mutex.hpp`, and more.

**Other header groups** — `serializer/` (schema-aware (de)serialization), `json/`, `hash/`.

### Coroutine Model

The library uses `BasicCoro` (fetched via CMake FetchContent from `ondra-novak/basic_coro`):

- `awaitable<T>` — standard awaitable wrapper
- `StrategyFragment` — fire-and-forget coroutine (used for event handlers)
- `StrategyFunction<T>` — coroutine returning T
- `coroutine` = `coro::coroutine<void>`

Strategies are coroutines launched under a `BacktestExecutor` (single-threaded event loop). All async work uses `co_await`.

### Implementation Layer (`src/quarkbot/` → `quarkbot::impl`)

Compiled `.cpp` sources, organized into component subdirectories (each its own `CMakeLists.txt`, several gated behind build options):

- **`backtest/`** — the simulation engine: `SimExchange` / `SimAccount` / `SimTradableInstrument` (in-memory simulation), `BacktestExecutor` (thread-local coroutine event loop that drives simulation time), `SimExecutor` (fills orders against market data with configurable slippage), data sources (`MergedDataSource`, `MinuteDataSource`), symbology mapping, and CSV exec-report output.
- **`common/`** — cross-cutting runtime: `logger`, `MemStorage` (in `mem_storage.hpp`), `order_trigger`, `somodule` (shared-object plugin loader), `thread_executor` (`ThreadExecutor`).
- **`streaming/`** (header-only) — `LockFreePublisher<T>` (ring-buffer publisher), `QueueEventStream` (buffered subscriber), publisher managers, orderbook state, bar-building lambdas, stream mapping.
- **`network/`** (opt: `QUARKBOT_NETWORK`) — SSL, WebSocket, REST/HTTP client, URL/base64/string utils.
- **`tardis/`** — `TardisTradesDataSource` / `TardisQuotesDataSource`, tardis.dev CSV export importers, wired to the `tardis.trades` / `tardis.quotes` config keys.
- **`trth/`** — Refinitiv TRTH event/raw source importers.
- **`algoseek/`** — `AlgoseekDataSource`, Algoseek US equity "Trades Only" importer, wired to the `algoseek` config key.
- **`leveldb/`** (opt: `QUARKBOT_LEVELDB`) — `LevelDBStorage` key-value backend.

### Strategies and Executables

- **`src/strategies/`** — reusable strategy logic compiled as `*_strategy` libraries: `print_events/` and `trending/`. These link `quarkbot::sdk` and contain the actual trading coroutines.
- **`src/exec/`** — thin executable drivers (built as `build/bin/*`) that wire a strategy library to `quarkbot::impl` + `quarkbot::sdk` and run a backtest (e.g. `print_events_backtest`, `trending_fast_test`). Sample data such as `btcusd.csv` lives here.

### Test Harness (`src/tests/check.h`)

Custom macro-based assertions — no external test framework:

```cpp
CHECK(expr)
CHECK_EQUAL(a, b)
CHECK_GREATER(a, b)
CHECK_EXCEPTION(expr, ExceptionType)
```

Each test source listed in the `BASIC_TESTS` set in `src/tests/CMakeLists.txt` becomes a separate binary named `test_<filename>` (registered with ctest as `tests/<filename>`) and links `quarkbot::sdk` + `quarkbot::impl`. Adding a new test means adding its `.cpp` to that list. A shared-object plugin fixture for the `somodule` loader lives in `src/tests/plugins/`.

## Key Conventions

- All public interfaces use `shared_ptr` aliases (`PExchange`, `PAccount`, etc.) — prefer these over raw `std::shared_ptr<IExchange>`.
- Financial values use `Decimal` from `quarkbot/decimal.hpp`, not `double`.
- Streaming events (quotes, trades, bars) are subscribed via `IMarketInstrument::subscribe*()` returning `EventStream<T>`, then consumed with `co_await stream.next(ref)`.
- The `BacktestExecutor` is single-threaded; strategies must not block — use `co_await` for all waits.
- `ThreadExecutor` wraps `BacktestExecutor` for multi-threaded scenarios.
