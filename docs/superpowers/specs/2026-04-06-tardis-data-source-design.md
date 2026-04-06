# Tardis Data Source & Merged Data Source — Design Spec

**Date:** 2026-04-06  
**Scope:** L1 replay data (quotes + trades) from Tardis.dev CSV files; chronological merging of multiple data sources.

---

## Overview

Extend the backtesting system with two new concrete `IBacktestDataSource` implementations:

- **`TardisTradesDataSource`** — reads Tardis `trades` CSV.gz files
- **`TardisQuotesDataSource`** — reads Tardis `quotes` (best bid/ask) CSV.gz files

And one combinator:

- **`MergedDataSource`** — chronologically merges N `IBacktestDataSource` instances via a min-heap

Each Tardis data file is paired with a companion `.ini` file that carries both the data file path and the `IMarketInstrument::Info` definition for that symbol. A new `ConfiguredDataSource` base class handles all format-agnostic concerns (INI parsing, gz reading, instrument info).

---

## Interface Change

`UnderlyingCurrency` (used by `IMarketInstrument::Info` for `quote_currency`, `pnl_currency`, `asset_wallet`) holds a `const IExchange*` and must be created via `exchange.create_currency()`. A data source reading a `.ini` file only knows currency *names* — it cannot construct `UnderlyingCurrency` without an exchange instance.

Therefore, introduce an `InstrumentSpec` struct that holds currency names as plain strings, with a `resolve(IExchange&)` method to produce the final `IMarketInstrument::Info`:

```cpp
// Added to src/ifc/backtest_data_source.hpp
struct InstrumentSpec {
    std::string name;
    InstrumentType type = InstrumentType::spot;
    std::string quote_currency;
    std::string pnl_currency;
    std::optional<std::string> asset_wallet;   // nullopt for contracts
    Decimal min_lot_size;
    Decimal lot_size_increment;
    Decimal price_increment;
    Decimal min_volume;
    Decimal leverage;
    Decimal fee_rate_maker;
    Decimal fee_rate_taker;
    Decimal multiplier = 1;
    Decimal tick_scale = 1;

    IMarketInstrument::Info resolve(IExchange &exchange) const;
};
```

`get_instrument_infos()` virtual method returns `InstrumentSpec` objects:

```cpp
virtual std::vector<InstrumentSpec> get_instrument_infos() { return {}; }
```

No existing implementations need to change. `MergedDataSource` overrides this to aggregate and deduplicate by `name` from all children.

---

## Companion INI File Format

Each data file has a companion `.ini` file at the same path with `.ini` extension. The user passes the `.ini` path to the constructor.

```ini
[source]
# Optional. Defaults to same path as .ini but with .csv.gz extension.
file=binance_trades_2024-01-15_BTCUSDT.csv.gz

[instrument]
# Required fields
name=BTCUSDT
type=spot                   # spot | contract | inverse_contract
quote_currency=USD
pnl_currency=USD
min_lot_size=0.00001
lot_size_increment=0.00001
price_increment=0.01

# Optional fields (default values shown)
asset_wallet=BTC            # omit entirely for contracts/futures → nullopt
min_volume=0
leverage=0                  # 0 = spot
fee_rate_maker=0
fee_rate_taker=0
multiplier=1
tick_scale=1
```

**Defaulting rule:** Any optional field absent from the INI uses the default value of the corresponding `ContractInfo` / `IMarketInstrument::Info` member.

**Reusability:** The INI+`source=` pattern lives in `ConfiguredDataSource` and is not Tardis-specific. Future formats (Binance Vision, HistData) subclass `ConfiguredDataSource` and get INI/gz/instrument-info handling for free.

---

## Class Structure

### `ConfiguredDataSource` — `src/impl/configured_data_source.hpp/.cpp`

Responsibilities:
- Parse the INI file at construction; throw on missing required fields
- Resolve the source file path (`source.file` or substitute `.csv.gz` extension)
- Open and manage the gzip stream via zlib (`gzFile`)
- Implement `get_instrument_infos()` returning the single parsed `Info`
- Expose `read_line(std::string&) → bool` for subclasses

```cpp
class ConfiguredDataSource : public IBacktestDataSource {
public:
    explicit ConfiguredDataSource(std::filesystem::path ini_path);
    ~ConfiguredDataSource();
    std::vector<IMarketInstrument::Info> get_instrument_infos() override;
protected:
    bool read_line(std::string &out);
    const IMarketInstrument::Info &instrument_info() const { return _info; }
private:
    IMarketInstrument::Info _info;
    gzFile _gz = nullptr;
};
```

INI parsing is done with a minimal hand-rolled key=value parser (no external library). Section headers (`[source]`, `[instrument]`) are parsed to scope keys.

---

### `TardisTradesDataSource` — `src/impl/tardis_data_source.hpp/.cpp`

Inherits `ConfiguredDataSource`. Implements `next_event()`:

1. On first call, read and parse the CSV header row to discover column indices for `timestamp`, `price`, `amount`.
2. On each subsequent call, read one row, parse fields by index, return:
   ```
   Event { timestamp, instrument_name, Trade { price, amount, timestamp } }
   ```
3. Return `std::nullopt` on EOF or parse error.

Tardis timestamp format: nanosecond Unix epoch (integer). Converted to `std::chrono::system_clock::time_point` via `duration_cast`.

```cpp
class TardisTradesDataSource : public ConfiguredDataSource {
public:
    using ConfiguredDataSource::ConfiguredDataSource;
    std::optional<Event> next_event() override;
private:
    bool _header_parsed = false;
    int _col_timestamp = -1, _col_price = -1, _col_amount = -1;
};
```

---

### `TardisQuotesDataSource` — `src/impl/tardis_data_source.hpp/.cpp`

Same file as `TardisTradesDataSource`. Inherits `ConfiguredDataSource`. Discovers columns: `timestamp`, `bidPrice`, `bidSize`, `askPrice`, `askSize`. Returns:

```
Event { timestamp, instrument_name, Quote { bid, bid_size, ask, ask_size, timestamp } }
```

```cpp
class TardisQuotesDataSource : public ConfiguredDataSource {
public:
    using ConfiguredDataSource::ConfiguredDataSource;
    std::optional<Event> next_event() override;
private:
    bool _header_parsed = false;
    int _col_timestamp = -1;
    int _col_bid_price = -1, _col_bid_size = -1;
    int _col_ask_price = -1, _col_ask_size = -1;
};
```

---

### `MergedDataSource` — `src/impl/merged_data_source.hpp/.cpp`

Chronological k-way merge of N `IBacktestDataSource` instances.

```cpp
class MergedDataSource : public IBacktestDataSource {
public:
    explicit MergedDataSource(std::vector<std::shared_ptr<IBacktestDataSource>> sources);
    std::optional<Event> next_event() override;
    std::vector<IMarketInstrument::Info> get_instrument_infos() override;
private:
    struct PeekedEvent {
        Event event;
        std::size_t source_idx;
        bool operator>(const PeekedEvent &o) const { return event.time > o.event.time; }
    };
    std::vector<std::shared_ptr<IBacktestDataSource>> _sources;
    std::priority_queue<PeekedEvent, std::vector<PeekedEvent>, std::greater<PeekedEvent>> _heap;
};
```

**Construction:** For each source, call `next_event()`; if it returns a value, push `PeekedEvent` onto heap.

**`next_event()`:**
1. If heap empty → return `std::nullopt`
2. Pop minimum `PeekedEvent`
3. Refill: call `next_event()` on `_sources[source_idx]`; if value, push to heap
4. Return the popped event

**`get_instrument_infos()`:** Collect from all children, deduplicate by `name` (first occurrence wins).

---

## Data Flow Example

```cpp
// User code
auto trades = std::make_shared<TardisTradesDataSource>("data/BTCUSDT_trades.ini");
auto quotes = std::make_shared<TardisQuotesDataSource>("data/BTCUSDT_quotes.ini");
auto merged = std::make_shared<MergedDataSource>(
    std::vector<std::shared_ptr<IBacktestDataSource>>{trades, quotes}
);

std::array wallet{ std::pair{"USD"s, 10000_dec} };
Backtest bt(merged, "backtest", wallet);

for (auto &spec : merged->get_instrument_infos())
    bt.add_instrument(spec.resolve(bt.get_exchange()));

bt.run(my_strategy(bt.get_context()));
```

---

## CMake Changes

`src/impl/CMakeLists.txt`:
```cmake
find_package(ZLIB REQUIRED)

target_sources(quarkbot_impl PRIVATE
    configured_data_source.cpp
    tardis_data_source.cpp
    merged_data_source.cpp
)

target_link_libraries(quarkbot_impl
    PUBLIC BasicCoro::basic_coro
    PRIVATE ZLIB::ZLIB
)
```

---

## File Layout

```
src/impl/
  configured_data_source.hpp   # ConfiguredDataSource base
  configured_data_source.cpp
  tardis_data_source.hpp       # TardisTradesDataSource, TardisQuotesDataSource
  tardis_data_source.cpp
  merged_data_source.hpp       # MergedDataSource
  merged_data_source.cpp
src/ifc/
  backtest_data_source.hpp     # add InstrumentSpec struct + get_instrument_infos() default method
```

---

## Out of Scope

- L2 (`OrderBookIncrement`) — deferred
- Binance Vision / HistData sources — future, will reuse `ConfiguredDataSource`
- Multi-day file sets (glob/auto-discover) — future; user wraps with `MergedDataSource` manually for now
