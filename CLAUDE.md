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

# Run a single test binary
./build/tests/test_order_test
./build/tests/test_decimal_test

# Build with clang (edit default_build_profile.conf or pass directly)
cmake -DCMAKE_CXX_COMPILER=clang++ ..
```

Test binaries are in `build/tests/`, strategy executables in `build/bin/`.

To add a compiler flag override, edit `default_build_profile.conf` (one `-DVariable=Value` per line).

## Architecture

The codebase follows a strict layered design:

```
User Strategies
    └── Core Interfaces (src/ifc/)        ← abstract contracts only
        └── Implementations (src/impl/)   ← simulation/backtest engines
            └── Utilities (src/utils/)    ← decimal, pubsub, signals, queues
                └── Network (src/libs/network/)  ← SSL, WebSocket, REST
```

### Interface Layer (`src/ifc/`)

All interfaces live here. Key types defined in `defs.hpp`:

- `PExchange` / `IExchange` — entry point; resolves instruments and accounts
- `PAccount` / `IAccount` — wallet and balance queries
- `PTradableInstrument` / `ITradableInstrument` — order placement and management
- `PMarketInstrument` / `IMarketInstrument` — market data subscriptions and metadata
- `PExecutionWorker` / `IExecutionWorker` — coroutine scheduler; `schedule()` transfers execution context
- `PBacktestDataSource` / `IBacktestDataSource` — historical data feed for backtesting
- `IEventStream<ViewType>` / `IPublisher<StreamTypeClass>` — typed event streaming

The `Order` class (`order.hpp`) is a state machine that tracks fills and status transitions. `Hub<T>` (`hub.hpp`) is a coroutine synchronization primitive (push/pop).

### Coroutine Model

The library uses `BasicCoro` (fetched via CMake FetchContent from `ondra-novak/basic_coro`):

- `awaitable<T>` — standard awaitable wrapper
- `StrategyFragment` — fire-and-forget coroutine (used for event handlers)
- `StrategyFunction<T>` — coroutine returning T
- `coroutine` = `coro::coroutine<void>`

Strategies are coroutines launched under a `BacktestExecutor` (single-threaded event loop). All async work uses `co_await`.

### Implementation Layer (`src/impl/`)

- `SimExchange` / `SimAccount` / `SimTradableInstrument` — in-memory simulation
- `BacktestExecutor` — thread-local coroutine event loop; drives simulation time
- `SimExecutor` — fills orders against market data with configurable slippage
- Data sources: `TardisDataSource`, `MMBotDataSource`, `MergedDataSource`, `ConfiguredDataSource`
- `LockFreePublisher<T>` — ring-buffer publisher; `QueueEventStream` — buffered subscriber
- `MemStorage` / `LevelDBStorage` — key-value storage implementations

### Utilities (`src/utils/`)

- `decimal.hpp` — fixed-point decimal for financial arithmetic (avoid floating point for prices/quantities)
- `pubsub.hpp` / `signals.hpp` / `signals_async.hpp` — pub-sub and signal/slot
- `lockfree_queue.hpp` — SPSC/MPSC lock-free queue
- `scheduled_queue.hpp` — priority queue with time-based execution
- `acb.hpp` — Average Cost Basis tracking

### Technical Analysis (`src/ta/`)

`ema.hpp` (EMA) and `bb_ema.hpp` (Bollinger Bands over EMA).

### Test Harness (`src/tests/check.h`)

Custom macro-based assertions — no external test framework:

```cpp
CHECK(expr)
CHECK_EQUAL(a, b)
CHECK_GREATER(a, b)
CHECK_EXCEPTION(expr, ExceptionType)
```

Each `.cpp` in `src/tests/` becomes a separate test binary named `test_<filename>`.

## Key Conventions

- All public interfaces use `shared_ptr` aliases (`PExchange`, `PAccount`, etc.) — prefer these over raw `std::shared_ptr<IExchange>`.
- Financial values use `Decimal` from `utils/decimal.hpp`, not `double`.
- Streaming events (quotes, trades, bars) are subscribed via `IMarketInstrument::subscribe*()` returning `EventStream<T>`, then consumed with `co_await stream.next(ref)`.
- The `BacktestExecutor` is single-threaded; strategies must not block — use `co_await` for all waits.
- `ThreadExecutor` wraps `BacktestExecutor` for multi-threaded scenarios.
