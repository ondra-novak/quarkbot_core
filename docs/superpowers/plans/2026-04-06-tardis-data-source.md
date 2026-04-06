# Tardis Data Source & MergedDataSource Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `TardisTradesDataSource`, `TardisQuotesDataSource`, and `MergedDataSource` to enable L1 backtesting from Tardis.dev gzip CSV files.

**Architecture:** A `ConfiguredDataSource` base class handles INI companion file parsing and gzip stream management via zlib. Tardis subclasses only implement CSV row parsing. `MergedDataSource` does a k-way min-heap merge of N sources. `InstrumentSpec` (string-based currency names) is introduced as an exchange-independent description that resolves to `IMarketInstrument::Info` via `IExchange::create_currency()`.

**Tech Stack:** C++23, zlib (gzopen/gzread), standard library only (no external CSV/INI libraries).

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Modify | `src/ifc/backtest_data_source.hpp` | Add `InstrumentSpec` struct + `get_instrument_infos()` virtual |
| Modify | `src/ifc/exchange.hpp` | Add `virtual create_currency(string_view) const = 0` |
| Modify | `src/impl/simexchange.hpp` | Override `create_currency` from `IExchange` |
| Modify | `src/impl/simexchange.cpp` | Implement the const override |
| Create | `src/impl/configured_data_source.hpp` | `ConfiguredDataSource` class declaration |
| Create | `src/impl/configured_data_source.cpp` | INI parsing, gz stream, `InstrumentSpec::resolve()` |
| Create | `src/impl/tardis_data_source.hpp` | `TardisTradesDataSource`, `TardisQuotesDataSource` declarations |
| Create | `src/impl/tardis_data_source.cpp` | CSV header detection + row parsing for trades and quotes |
| Create | `src/impl/merged_data_source.hpp` | `MergedDataSource` declaration |
| Create | `src/impl/merged_data_source.cpp` | k-way min-heap merge |
| Modify | `src/impl/CMakeLists.txt` | Add zlib, new source files, test targets |
| Create | `src/tests/tardis_source_test.cpp` | Tests for ConfiguredDataSource + both Tardis sources |
| Create | `src/tests/merged_source_test.cpp` | Tests for MergedDataSource |

---

## Task 1: Add `InstrumentSpec` to `IBacktestDataSource` and `create_currency` to `IExchange`

**Files:**
- Modify: `src/ifc/backtest_data_source.hpp`
- Modify: `src/ifc/exchange.hpp`
- Modify: `src/impl/simexchange.hpp`
- Modify: `src/impl/simexchange.cpp`

- [ ] **Step 1: Write the failing test** — create `src/tests/instrument_spec_test.cpp`

```cpp
#include "impl/simexchange.hpp"
#include "ifc/backtest_data_source.hpp"
#include "tests/check.h"

int main() {
    auto exchange = std::make_shared<quarkbot::SimExchange>();

    quarkbot::InstrumentSpec spec;
    spec.name = "BTCUSDT";
    spec.type = quarkbot::InstrumentType::spot;
    spec.quote_currency = "USD";
    spec.pnl_currency = "USD";
    spec.asset_wallet = "BTC";
    spec.min_lot_size = quarkbot::Decimal(1,  -5);   // 0.00001
    spec.lot_size_increment = quarkbot::Decimal(1, -5);
    spec.price_increment = quarkbot::Decimal(1, -2);  // 0.01
    spec.min_volume = {};
    spec.leverage = {};
    spec.fee_rate_maker = quarkbot::Decimal(1, -3);   // 0.001
    spec.fee_rate_taker = quarkbot::Decimal(1, -3);
    spec.multiplier = quarkbot::Decimal(1);
    spec.tick_scale = quarkbot::Decimal(1);

    auto info = spec.resolve(*exchange);

    CHECK_EQUAL(info.name, std::string("BTCUSDT"));
    CHECK(info.type == quarkbot::InstrumentType::spot);
    CHECK_EQUAL(info.quote_currency.id, std::string("USD"));
    CHECK_EQUAL(info.pnl_currency.id, std::string("USD"));
    CHECK(info.asset_wallet.has_value());
    CHECK_EQUAL(info.asset_wallet->id, std::string("BTC"));
    CHECK(info.fee_rate_maker == quarkbot::Decimal(1, -3));
}
```

- [ ] **Step 2: Add test to CMakeLists and attempt build to confirm it fails**

In `src/tests/CMakeLists.txt`, add `instrument_spec_test.cpp` to `testFiles` and link `quarkbot_impl`:

```cmake
set(testFiles
    compile_test.cpp
    decimal_test.cpp
    pubsub.cpp
    instrument_spec_test.cpp
)
# ... existing foreach loop, then add at end:
target_link_libraries(tests_instrument_spec_test.cpp PRIVATE quarkbot_impl)
```

Wait — the existing CMakeLists uses a loop. Replace with a more flexible approach. Change `src/tests/CMakeLists.txt` to:

```cmake
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/)

set(BASIC_TESTS
    compile_test.cpp
    decimal_test.cpp
    pubsub.cpp
)

set(IMPL_TESTS
    instrument_spec_test.cpp
    tardis_source_test.cpp
    merged_source_test.cpp
)

foreach (testFile ${BASIC_TESTS})
    string(REGEX MATCH "([^\/]+$)" filename ${testFile})
    string(REGEX MATCH "[^.]*" executable_name tests_${filename})
    add_executable(${executable_name} ${testFile})
    target_link_libraries(${executable_name} PRIVATE BasicCoro::basic_coro)
    target_compile_definitions(${executable_name} PRIVATE
        MODULE_PATH="${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    )
    add_test(NAME "tests/${filename}" COMMAND ${executable_name})
endforeach()

foreach (testFile ${IMPL_TESTS})
    string(REGEX MATCH "([^\/]+$)" filename ${testFile})
    string(REGEX MATCH "[^.]*" executable_name tests_${filename})
    add_executable(${executable_name} ${testFile})
    target_link_libraries(${executable_name} PRIVATE quarkbot_impl)
    target_compile_definitions(${executable_name} PRIVATE
        MODULE_PATH="${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    )
    add_test(NAME "tests/${filename}" COMMAND ${executable_name})
endforeach()
```

Run: `cmake -B build && cmake --build build/  2>&1 | head -30`
Expected: compile error — `InstrumentSpec` not defined.

- [ ] **Step 3: Add `InstrumentSpec` to `src/ifc/backtest_data_source.hpp`**

Replace the entire file with:

```cpp
#pragma once

#include "market_events.hpp"
#include "ifc/types.hpp"
#include "utils/decimal.hpp"
#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace quarkbot {

class IExchange;
class IMarketInstrument;
struct UnderlyingCurrency;

struct InstrumentSpec {
    std::string name;
    InstrumentType type = InstrumentType::spot;
    std::string quote_currency;
    std::string pnl_currency;
    std::optional<std::string> asset_wallet;
    Decimal min_lot_size = {};
    Decimal lot_size_increment = {};
    Decimal price_increment = {};
    Decimal min_volume = {};
    Decimal leverage = {};
    Decimal fee_rate_maker = {};
    Decimal fee_rate_taker = {};
    Decimal multiplier = Decimal(1);
    Decimal tick_scale = Decimal(1);

    IMarketInstrument::Info resolve(IExchange &exchange) const;
};

class IBacktestDataSource {
public:
    using EventData = std::variant<Quote, Trade, OrderBookIncrement>;
    struct Event {
        std::chrono::system_clock::time_point time;
        std::string instrument;
        EventData payload;
    };

    virtual std::optional<Event> next_event() = 0;
    virtual std::vector<InstrumentSpec> get_instrument_infos() { return {}; }
    virtual ~IBacktestDataSource() = default;
};

} // namespace quarkbot
```

- [ ] **Step 4: Add `create_currency` to `src/ifc/exchange.hpp`**

Add the pure virtual method after `get_all_currencies()`:

```cpp
virtual UnderlyingCurrency create_currency(std::string_view name) const = 0;
```

The full `exchange.hpp` becomes:

```cpp
#pragma once
#include "defs.hpp"
#include "ifc/underlying.hpp"
#include <string_view>
#include <vector>

namespace quarkbot {

class IExchange {
public:
    virtual ~IExchange() = default;
    virtual PAccount create_account(const std::string &name, const std::string &credentials) const = 0;
    virtual std::vector<PMarketInstrument> get_market_instruments() const = 0;
    virtual std::vector<UnderlyingCurrency> get_all_currencies() const = 0;
    virtual std::string_view get_name() const = 0;
    virtual UnderlyingCurrency create_currency(std::string_view name) const = 0;
};

} // namespace quarkbot
```

- [ ] **Step 5: Override `create_currency` in `SimExchange`**

In `src/impl/simexchange.hpp`, add the override declaration (the existing method with default arg stays, add the const override):

```cpp
UnderlyingCurrency create_currency(std::string_view name) const override;
UnderlyingCurrency create_currency(std::string_view name, bool is_unified);
```

In `src/impl/simexchange.cpp`, add the const override (the existing non-const method stays):

```cpp
UnderlyingCurrency SimExchange::create_currency(std::string_view name) const {
    return UnderlyingCurrency{std::string(name), std::string(name), this};
}
```

- [ ] **Step 6: Implement `InstrumentSpec::resolve()` in `src/impl/configured_data_source.cpp`**

This file doesn't exist yet — create a stub just to compile `resolve()`:

```cpp
#include "configured_data_source.hpp"
#include "ifc/exchange.hpp"
#include "ifc/market_instrument.hpp"

namespace quarkbot {

IMarketInstrument::Info InstrumentSpec::resolve(IExchange &exchange) const {
    IMarketInstrument::Info info;
    info.name = name;
    info.type = type;
    info.quote_currency = exchange.create_currency(quote_currency);
    info.pnl_currency = exchange.create_currency(pnl_currency);
    if (asset_wallet) info.asset_wallet = exchange.create_currency(*asset_wallet);
    info.min_lot_size = min_lot_size;
    info.lot_size_increment = lot_size_increment;
    info.price_increment = price_increment;
    info.min_volume = min_volume;
    info.leverage = leverage;
    info.fee_rate_maker = fee_rate_maker;
    info.fee_rate_taker = fee_rate_taker;
    info.multiplier = multiplier;
    info.tick_scale = tick_scale;
    return info;
}

} // namespace quarkbot
```

Also create a minimal `src/impl/configured_data_source.hpp` stub so it can be included:

```cpp
#pragma once
#include "ifc/backtest_data_source.hpp"
#include <filesystem>

namespace quarkbot {

class ConfiguredDataSource : public IBacktestDataSource {
public:
    explicit ConfiguredDataSource(std::filesystem::path ini_path);
    ~ConfiguredDataSource() override;
    std::vector<InstrumentSpec> get_instrument_infos() override;
    std::optional<Event> next_event() override = 0;
protected:
    bool read_line(std::string &out);
    const InstrumentSpec &instrument_spec() const { return _spec; }
private:
    InstrumentSpec _spec;
    void *_gz = nullptr;   // gzFile (void* to avoid including zlib.h in header)
};

} // namespace quarkbot
```

Add `configured_data_source.cpp` to `src/impl/CMakeLists.txt` sources (done in Task 6).

- [ ] **Step 7: Build and run the test**

```bash
cmake --build build/ --target tests_instrument_spec_test.cpp && ./build/tests/tests_instrument_spec_test.cpp
```

Expected output: all `Passed:` lines, exit 0.

- [ ] **Step 8: Commit**

```bash
git add src/ifc/backtest_data_source.hpp src/ifc/exchange.hpp \
        src/impl/simexchange.hpp src/impl/simexchange.cpp \
        src/impl/configured_data_source.hpp src/impl/configured_data_source.cpp \
        src/tests/instrument_spec_test.cpp src/tests/CMakeLists.txt \
        src/impl/CMakeLists.txt
git commit -m "feat: add InstrumentSpec, get_instrument_infos, IExchange::create_currency"
```

---

## Task 2: `ConfiguredDataSource` — INI parsing and gzip stream

**Files:**
- Modify: `src/impl/configured_data_source.hpp`
- Modify: `src/impl/configured_data_source.cpp`

This task completes the base class. The INI parser uses a simple hand-rolled scanner: skip blank lines and `#` comment lines, parse `[section]` headers, parse `key=value` pairs.

- [ ] **Step 1: Write failing test for INI parsing** — add to `src/tests/tardis_source_test.cpp`

```cpp
#include "impl/tardis_data_source.hpp"
#include "impl/simexchange.hpp"
#include "tests/check.h"
#include <zlib.h>
#include <cstdio>
#include <fstream>

// Helper: write content to a .csv.gz file at path
static void write_gz(const std::string &path, std::string_view content) {
    gzFile f = gzopen(path.c_str(), "wb");
    if (!f) { std::cerr << "Cannot open gz for write: " << path << std::endl; exit(1); }
    gzwrite(f, content.data(), static_cast<unsigned>(content.size()));
    gzclose(f);
}

// Helper: write a text file at path
static void write_file(const std::string &path, std::string_view content) {
    std::ofstream f(path);
    f << content;
}

static const std::string INI_SPOT = R"(
[instrument]
name=BTCUSDT
type=spot
quote_currency=USD
pnl_currency=USD
asset_wallet=BTC
min_lot_size=0.00001
lot_size_increment=0.00001
price_increment=0.01
fee_rate_maker=0.001
fee_rate_taker=0.001
)";

static const std::string TRADES_CSV =
    "exchange,symbol,timestamp,localTimestamp,id,side,price,amount\n"
    "binance,BTCUSDT,1617235200000000000,1617235200001000000,1,buy,58000.50,0.001\n"
    "binance,BTCUSDT,1617235260000000000,1617235260001000000,2,sell,58100.00,0.002\n";

int main() {
    write_file("/tmp/test_trades.ini",
        std::string(INI_SPOT) + "\n[source]\nfile=/tmp/test_trades.csv.gz\n");
    write_gz("/tmp/test_trades.csv.gz", TRADES_CSV);

    // test instrument spec from INI
    quarkbot::TardisTradesDataSource src("/tmp/test_trades.ini");
    auto specs = src.get_instrument_infos();
    CHECK_EQUAL(specs.size(), std::size_t(1));
    CHECK_EQUAL(specs[0].name, std::string("BTCUSDT"));
    CHECK(specs[0].type == quarkbot::InstrumentType::spot);
    CHECK_EQUAL(specs[0].quote_currency, std::string("USD"));
    CHECK_EQUAL(specs[0].pnl_currency, std::string("USD"));
    CHECK(specs[0].asset_wallet.has_value());
    CHECK_EQUAL(*specs[0].asset_wallet, std::string("BTC"));
    CHECK(specs[0].fee_rate_maker == quarkbot::Decimal(1, -3));

    // test reading trade events
    auto e1 = src.next_event();
    CHECK(e1.has_value());
    CHECK_EQUAL(e1->instrument, std::string("BTCUSDT"));
    CHECK(std::holds_alternative<quarkbot::Trade>(e1->payload));
    auto &t1 = std::get<quarkbot::Trade>(e1->payload);
    CHECK(t1.price == quarkbot::Decimal::from_string("58000.50"));
    CHECK(t1.size == quarkbot::Decimal::from_string("0.001"));

    auto e2 = src.next_event();
    CHECK(e2.has_value());
    auto &t2 = std::get<quarkbot::Trade>(e2->payload);
    CHECK(t2.price == quarkbot::Decimal::from_string("58100.00"));
    CHECK(t2.size == quarkbot::Decimal::from_string("0.002"));

    auto e3 = src.next_event();
    CHECK(!e3.has_value());   // EOF

    std::remove("/tmp/test_trades.ini");
    std::remove("/tmp/test_trades.csv.gz");

    std::cout << "All tardis trades tests passed" << std::endl;
}
```

- [ ] **Step 2: Run build to confirm it fails**

```bash
cmake --build build/ --target tests_tardis_source_test.cpp 2>&1 | head -20
```

Expected: compile error — `TardisTradesDataSource` not defined.

- [ ] **Step 3: Complete `configured_data_source.hpp`**

Replace the stub with the full declaration:

```cpp
#pragma once
#include "ifc/backtest_data_source.hpp"
#include <filesystem>
#include <string>

// Forward-declare gzFile without including zlib.h in the header
struct gzFile_s;

namespace quarkbot {

class ConfiguredDataSource : public IBacktestDataSource {
public:
    explicit ConfiguredDataSource(std::filesystem::path ini_path);
    ~ConfiguredDataSource() override;
    std::vector<InstrumentSpec> get_instrument_infos() override;
protected:
    bool read_line(std::string &out);
    const InstrumentSpec &instrument_spec() const { return _spec; }
private:
    InstrumentSpec _spec;
    gzFile_s *_gz = nullptr;
    void parse_ini(const std::filesystem::path &ini_path);
};

} // namespace quarkbot
```

- [ ] **Step 4: Implement `configured_data_source.cpp`** — replace the stub with full implementation:

```cpp
#include "configured_data_source.hpp"
#include "ifc/exchange.hpp"
#include "ifc/market_instrument.hpp"
#include <zlib.h>
#include <fstream>
#include <stdexcept>
#include <string>

namespace quarkbot {

IMarketInstrument::Info InstrumentSpec::resolve(IExchange &exchange) const {
    IMarketInstrument::Info info;
    static_cast<ContractInfo &>(info).type = type;
    static_cast<ContractInfo &>(info).multiplier = multiplier;
    static_cast<ContractInfo &>(info).tick_scale = tick_scale;
    info.name = name;
    info.quote_currency = exchange.create_currency(quote_currency);
    info.pnl_currency = exchange.create_currency(pnl_currency);
    if (asset_wallet) info.asset_wallet = exchange.create_currency(*asset_wallet);
    info.min_lot_size = min_lot_size;
    info.lot_size_increment = lot_size_increment;
    info.price_increment = price_increment;
    info.min_volume = min_volume;
    info.leverage = leverage;
    info.fee_rate_maker = fee_rate_maker;
    info.fee_rate_taker = fee_rate_taker;
    return info;
}

// Strip trailing \r\n from a string in-place
static void strip_newline(std::string &s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
}

// Parse "key=value", return false if line has no '='
static bool parse_kv(const std::string &line, std::string &key, std::string &value) {
    auto pos = line.find('=');
    if (pos == std::string::npos) return false;
    key = line.substr(0, pos);
    value = line.substr(pos + 1);
    return true;
}

void ConfiguredDataSource::parse_ini(const std::filesystem::path &ini_path) {
    std::ifstream f(ini_path);
    if (!f) throw std::runtime_error("Cannot open INI file: " + ini_path.string());

    std::string section;
    std::string line;
    std::filesystem::path source_file;

    while (std::getline(f, line)) {
        strip_newline(line);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            auto end = line.find(']');
            section = (end != std::string::npos) ? line.substr(1, end - 1) : line.substr(1);
            continue;
        }
        std::string key, val;
        if (!parse_kv(line, key, val)) continue;

        if (section == "source") {
            if (key == "file") source_file = val;
        } else if (section == "instrument") {
            if      (key == "name")               _spec.name = val;
            else if (key == "type") {
                if      (val == "spot")             _spec.type = InstrumentType::spot;
                else if (val == "contract")         _spec.type = InstrumentType::contract;
                else if (val == "inverse_contract") _spec.type = InstrumentType::inverse_contract;
            }
            else if (key == "quote_currency")     _spec.quote_currency = val;
            else if (key == "pnl_currency")       _spec.pnl_currency = val;
            else if (key == "asset_wallet")       _spec.asset_wallet = val;
            else if (key == "min_lot_size")       _spec.min_lot_size = Decimal::from_string(val);
            else if (key == "lot_size_increment") _spec.lot_size_increment = Decimal::from_string(val);
            else if (key == "price_increment")    _spec.price_increment = Decimal::from_string(val);
            else if (key == "min_volume")         _spec.min_volume = Decimal::from_string(val);
            else if (key == "leverage")           _spec.leverage = Decimal::from_string(val);
            else if (key == "fee_rate_maker")     _spec.fee_rate_maker = Decimal::from_string(val);
            else if (key == "fee_rate_taker")     _spec.fee_rate_taker = Decimal::from_string(val);
            else if (key == "multiplier")         _spec.multiplier = Decimal::from_string(val);
            else if (key == "tick_scale")         _spec.tick_scale = Decimal::from_string(val);
        }
    }

    // Validate required fields
    if (_spec.name.empty())
        throw std::runtime_error("INI missing [instrument] name=");
    if (_spec.quote_currency.empty())
        throw std::runtime_error("INI missing [instrument] quote_currency=");
    if (_spec.pnl_currency.empty())
        throw std::runtime_error("INI missing [instrument] pnl_currency=");

    // Resolve data file path
    if (source_file.empty())
        source_file = ini_path.parent_path() / (ini_path.stem().string() + ".csv.gz");

    _gz = gzopen(source_file.c_str(), "rb");
    if (!_gz)
        throw std::runtime_error("Cannot open gz file: " + source_file.string());
}

ConfiguredDataSource::ConfiguredDataSource(std::filesystem::path ini_path) {
    parse_ini(ini_path);
}

ConfiguredDataSource::~ConfiguredDataSource() {
    if (_gz) gzclose(reinterpret_cast<gzFile>(_gz));
}

std::vector<InstrumentSpec> ConfiguredDataSource::get_instrument_infos() {
    return {_spec};
}

bool ConfiguredDataSource::read_line(std::string &out) {
    out.clear();
    char buf[1024];
    while (true) {
        if (!gzgets(reinterpret_cast<gzFile>(_gz), buf, sizeof(buf)))
            return !out.empty();
        out += buf;
        // gzgets stops at newline or EOF; if we got a newline we're done
        if (!out.empty() && out.back() == '\n') {
            strip_newline(out);
            return true;
        }
        // no newline yet — buffer was full, loop for more
    }
}

} // namespace quarkbot
```

- [ ] **Step 5: Update `src/impl/CMakeLists.txt`** to add zlib and new sources:

```cmake
find_package(ZLIB REQUIRED)

add_library(quarkbot_impl
    mmbot_data_source.cpp
    backtest_executor.cpp
    simexecutor.cpp
    simexchange.cpp
    siminstrument.cpp
    simaccount.cpp
    simtradableinstrument.cpp
    backtest.cpp
    configured_data_source.cpp
    tardis_data_source.cpp
    merged_data_source.cpp
)

target_link_libraries(quarkbot_impl
    PUBLIC BasicCoro::basic_coro
    PRIVATE ZLIB::ZLIB
)
```

Also create empty stub files so CMake doesn't fail on missing sources:

```bash
touch /home/ondra/vscode/trading_interface/src/impl/tardis_data_source.cpp
touch /home/ondra/vscode/trading_interface/src/impl/tardis_data_source.hpp
touch /home/ondra/vscode/trading_interface/src/impl/merged_data_source.cpp
touch /home/ondra/vscode/trading_interface/src/impl/merged_data_source.hpp
```

- [ ] **Step 6: Build (test will still fail — Tardis classes not yet implemented)**

```bash
cmake -B build && cmake --build build/ --target tests_tardis_source_test.cpp 2>&1 | head -20
```

Expected: compile error — `TardisTradesDataSource` not defined.

---

## Task 3: `TardisTradesDataSource`

**Files:**
- Create: `src/impl/tardis_data_source.hpp`
- Create: `src/impl/tardis_data_source.cpp`

- [ ] **Step 1: Write `tardis_data_source.hpp`**

```cpp
#pragma once
#include "configured_data_source.hpp"
#include <string>

namespace quarkbot {

class TardisTradesDataSource : public ConfiguredDataSource {
public:
    using ConfiguredDataSource::ConfiguredDataSource;
    std::optional<Event> next_event() override;
private:
    bool _header_parsed = false;
    int _col_timestamp = -1;
    int _col_price = -1;
    int _col_amount = -1;
};

class TardisQuotesDataSource : public ConfiguredDataSource {
public:
    using ConfiguredDataSource::ConfiguredDataSource;
    std::optional<Event> next_event() override;
private:
    bool _header_parsed = false;
    int _col_timestamp = -1;
    int _col_bid_price = -1;
    int _col_bid_size = -1;
    int _col_ask_price = -1;
    int _col_ask_size = -1;
};

} // namespace quarkbot
```

- [ ] **Step 2: Implement `TardisTradesDataSource::next_event()` in `tardis_data_source.cpp`**

```cpp
#include "tardis_data_source.hpp"
#include "ifc/market_events.hpp"
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace quarkbot {

// Split a line by comma, return vector of string_view into the line.
// The line must remain valid while the views are used.
static std::vector<std::string_view> split_csv(std::string_view line) {
    std::vector<std::string_view> cols;
    while (true) {
        auto pos = line.find(',');
        cols.push_back(line.substr(0, pos));
        if (pos == std::string_view::npos) break;
        line.remove_prefix(pos + 1);
    }
    return cols;
}

// Parse a Tardis nanosecond timestamp to system_clock::time_point
static std::chrono::system_clock::time_point parse_ns_timestamp(std::string_view s) {
    long long ns = 0;
    for (char c : s) {
        if (c < '0' || c > '9') break;
        ns = ns * 10 + (c - '0');
    }
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::nanoseconds(ns)));
}

std::optional<IBacktestDataSource::Event> TardisTradesDataSource::next_event() {
    std::string line;

    if (!_header_parsed) {
        if (!read_line(line)) return std::nullopt;
        auto cols = split_csv(line);
        for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
            if (cols[i] == "timestamp")  _col_timestamp = i;
            else if (cols[i] == "price") _col_price = i;
            else if (cols[i] == "amount") _col_amount = i;
        }
        _header_parsed = true;
    }

    while (read_line(line)) {
        if (line.empty()) continue;
        auto cols = split_csv(line);
        int max_col = std::max({_col_timestamp, _col_price, _col_amount});
        if (max_col < 0 || static_cast<int>(cols.size()) <= max_col) continue;

        auto tp = parse_ns_timestamp(cols[static_cast<std::size_t>(_col_timestamp)]);
        Decimal price, amount;
        try {
            price  = Decimal::from_string(cols[static_cast<std::size_t>(_col_price)]);
            amount = Decimal::from_string(cols[static_cast<std::size_t>(_col_amount)]);
        } catch (...) { continue; }

        return Event{tp, instrument_spec().name, Trade{price, amount, tp}};
    }
    return std::nullopt;
}
```

- [ ] **Step 3: Build and run the trades part of the test**

```bash
cmake --build build/ --target tests_tardis_source_test.cpp && ./build/tests/tests_tardis_source_test.cpp
```

Expected: first batch of `Passed:` lines for trades. If the quotes portion hasn't been written yet the test may fail partway — that's fine, verify the trades checks pass first.

---

## Task 4: `TardisQuotesDataSource`

**Files:**
- Modify: `src/impl/tardis_data_source.cpp` (append)
- Modify: `src/tests/tardis_source_test.cpp` (append quotes test cases)

- [ ] **Step 1: Add quotes test cases to `src/tests/tardis_source_test.cpp`**

After the existing `main()` closing brace, there is no separate main — instead, extend the same `main()` to add quotes testing. Replace the existing `main()` with the full version below (or append before the final `return`):

```cpp
// --- add before final std::cout / return in main() ---

static const std::string QUOTES_CSV =
    "exchange,symbol,timestamp,localTimestamp,bidPrice,bidSize,askPrice,askSize\n"
    "binance,BTCUSDT,1617235200000000000,1617235200001000000,57999.00,1.5,58001.00,0.8\n"
    "binance,BTCUSDT,1617235260000000000,1617235260001000000,58050.00,2.0,58052.00,1.2\n";

write_file("/tmp/test_quotes.ini",
    std::string(INI_SPOT) + "\n[source]\nfile=/tmp/test_quotes.csv.gz\n");
write_gz("/tmp/test_quotes.csv.gz", QUOTES_CSV);

{
    quarkbot::TardisQuotesDataSource qsrc("/tmp/test_quotes.ini");
    auto specs = qsrc.get_instrument_infos();
    CHECK_EQUAL(specs.size(), std::size_t(1));

    auto q1 = qsrc.next_event();
    CHECK(q1.has_value());
    CHECK(std::holds_alternative<quarkbot::Quote>(q1->payload));
    auto &quote1 = std::get<quarkbot::Quote>(q1->payload);
    CHECK(quote1.bid == quarkbot::Decimal::from_string("57999.00"));
    CHECK(quote1.bid_size == quarkbot::Decimal::from_string("1.5"));
    CHECK(quote1.ask == quarkbot::Decimal::from_string("58001.00"));
    CHECK(quote1.ask_size == quarkbot::Decimal::from_string("0.8"));

    auto q2 = qsrc.next_event();
    CHECK(q2.has_value());
    auto &quote2 = std::get<quarkbot::Quote>(q2->payload);
    CHECK(quote2.bid == quarkbot::Decimal::from_string("58050.00"));

    auto q3 = qsrc.next_event();
    CHECK(!q3.has_value());
}

std::remove("/tmp/test_quotes.ini");
std::remove("/tmp/test_quotes.csv.gz");
```

- [ ] **Step 2: Implement `TardisQuotesDataSource::next_event()` — append to `tardis_data_source.cpp`**

```cpp
std::optional<IBacktestDataSource::Event> TardisQuotesDataSource::next_event() {
    std::string line;

    if (!_header_parsed) {
        if (!read_line(line)) return std::nullopt;
        auto cols = split_csv(line);
        for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
            if      (cols[i] == "timestamp") _col_timestamp = i;
            else if (cols[i] == "bidPrice")  _col_bid_price = i;
            else if (cols[i] == "bidSize")   _col_bid_size  = i;
            else if (cols[i] == "askPrice")  _col_ask_price = i;
            else if (cols[i] == "askSize")   _col_ask_size  = i;
        }
        _header_parsed = true;
    }

    while (read_line(line)) {
        if (line.empty()) continue;
        auto cols = split_csv(line);
        int max_col = std::max({_col_timestamp, _col_bid_price, _col_bid_size,
                                _col_ask_price, _col_ask_size});
        if (max_col < 0 || static_cast<int>(cols.size()) <= max_col) continue;

        auto tp = parse_ns_timestamp(cols[static_cast<std::size_t>(_col_timestamp)]);
        Decimal bid, bid_size, ask, ask_size;
        try {
            bid      = Decimal::from_string(cols[static_cast<std::size_t>(_col_bid_price)]);
            bid_size = Decimal::from_string(cols[static_cast<std::size_t>(_col_bid_size)]);
            ask      = Decimal::from_string(cols[static_cast<std::size_t>(_col_ask_price)]);
            ask_size = Decimal::from_string(cols[static_cast<std::size_t>(_col_ask_size)]);
        } catch (...) { continue; }

        return Event{tp, instrument_spec().name, Quote{bid, bid_size, ask, ask_size, tp}};
    }
    return std::nullopt;
}
```

- [ ] **Step 3: Build and run full Tardis test**

```bash
cmake --build build/ --target tests_tardis_source_test.cpp && ./build/tests/tests_tardis_source_test.cpp
```

Expected: all `Passed:` + "All tardis trades tests passed", exit 0.

- [ ] **Step 4: Commit**

```bash
git add src/impl/configured_data_source.hpp src/impl/configured_data_source.cpp \
        src/impl/tardis_data_source.hpp src/impl/tardis_data_source.cpp \
        src/impl/CMakeLists.txt src/tests/tardis_source_test.cpp \
        src/tests/CMakeLists.txt
git commit -m "feat: add ConfiguredDataSource, TardisTradesDataSource, TardisQuotesDataSource"
```

---

## Task 5: `MergedDataSource`

**Files:**
- Create: `src/impl/merged_data_source.hpp`
- Create: `src/impl/merged_data_source.cpp`
- Create: `src/tests/merged_source_test.cpp`

- [ ] **Step 1: Write failing test** — `src/tests/merged_source_test.cpp`

```cpp
#include "impl/merged_data_source.hpp"
#include "ifc/backtest_data_source.hpp"
#include "tests/check.h"
#include <chrono>
#include <memory>
#include <vector>

using namespace quarkbot;
using tp = std::chrono::system_clock::time_point;

// Stub source: returns pre-made events in order
struct StubSource : public IBacktestDataSource {
    std::vector<Event> events;
    std::size_t idx = 0;

    explicit StubSource(std::vector<Event> evts) : events(std::move(evts)) {}

    std::optional<Event> next_event() override {
        if (idx >= events.size()) return std::nullopt;
        return events[idx++];
    }

    std::vector<InstrumentSpec> get_instrument_infos() override {
        InstrumentSpec s;
        s.name = "SYM";
        s.quote_currency = "USD";
        s.pnl_currency = "USD";
        return {s};
    }
};

static tp make_tp(long long ms) {
    return tp(std::chrono::milliseconds(ms));
}

int main() {
    // Source A: events at t=100, t=300, t=500
    auto a = std::make_shared<StubSource>(std::vector<IBacktestDataSource::Event>{
        {make_tp(100), "SYM", Trade{Decimal(1), Decimal(1), make_tp(100)}},
        {make_tp(300), "SYM", Trade{Decimal(3), Decimal(1), make_tp(300)}},
        {make_tp(500), "SYM", Trade{Decimal(5), Decimal(1), make_tp(500)}},
    });

    // Source B: events at t=200, t=400
    auto b = std::make_shared<StubSource>(std::vector<IBacktestDataSource::Event>{
        {make_tp(200), "SYM", Trade{Decimal(2), Decimal(1), make_tp(200)}},
        {make_tp(400), "SYM", Trade{Decimal(4), Decimal(1), make_tp(400)}},
    });

    MergedDataSource merged({a, b});

    // Events must come out in time order: 100,200,300,400,500
    auto e1 = merged.next_event(); CHECK(e1.has_value()); CHECK(e1->time == make_tp(100));
    auto e2 = merged.next_event(); CHECK(e2.has_value()); CHECK(e2->time == make_tp(200));
    auto e3 = merged.next_event(); CHECK(e3.has_value()); CHECK(e3->time == make_tp(300));
    auto e4 = merged.next_event(); CHECK(e4.has_value()); CHECK(e4->time == make_tp(400));
    auto e5 = merged.next_event(); CHECK(e5.has_value()); CHECK(e5->time == make_tp(500));
    auto e6 = merged.next_event(); CHECK(!e6.has_value());

    // Test get_instrument_infos deduplication
    auto a2 = std::make_shared<StubSource>(std::vector<IBacktestDataSource::Event>{});
    auto b2 = std::make_shared<StubSource>(std::vector<IBacktestDataSource::Event>{});
    // Both return spec with name="SYM" — should deduplicate to 1
    MergedDataSource merged2({a2, b2});
    auto specs = merged2.get_instrument_infos();
    CHECK_EQUAL(specs.size(), std::size_t(1));
    CHECK_EQUAL(specs[0].name, std::string("SYM"));

    std::cout << "All merged source tests passed" << std::endl;
}
```

- [ ] **Step 2: Build to confirm failure**

```bash
cmake --build build/ --target tests_merged_source_test.cpp 2>&1 | head -10
```

Expected: compile error — `MergedDataSource` not defined.

- [ ] **Step 3: Write `src/impl/merged_data_source.hpp`**

```cpp
#pragma once
#include "ifc/backtest_data_source.hpp"
#include <memory>
#include <queue>
#include <vector>

namespace quarkbot {

class MergedDataSource : public IBacktestDataSource {
public:
    explicit MergedDataSource(std::vector<std::shared_ptr<IBacktestDataSource>> sources);
    std::optional<Event> next_event() override;
    std::vector<InstrumentSpec> get_instrument_infos() override;

private:
    struct PeekedEvent {
        Event event;
        std::size_t source_idx;
        bool operator>(const PeekedEvent &o) const { return event.time > o.event.time; }
    };

    std::vector<std::shared_ptr<IBacktestDataSource>> _sources;
    std::priority_queue<PeekedEvent, std::vector<PeekedEvent>, std::greater<PeekedEvent>> _heap;
};

} // namespace quarkbot
```

- [ ] **Step 4: Write `src/impl/merged_data_source.cpp`**

```cpp
#include "merged_data_source.hpp"
#include <algorithm>

namespace quarkbot {

MergedDataSource::MergedDataSource(std::vector<std::shared_ptr<IBacktestDataSource>> sources)
    : _sources(std::move(sources)) {
    for (std::size_t i = 0; i < _sources.size(); ++i) {
        auto ev = _sources[i]->next_event();
        if (ev) _heap.push(PeekedEvent{std::move(*ev), i});
    }
}

std::optional<IBacktestDataSource::Event> MergedDataSource::next_event() {
    if (_heap.empty()) return std::nullopt;
    auto top = _heap.top();
    _heap.pop();
    auto refill = _sources[top.source_idx]->next_event();
    if (refill) _heap.push(PeekedEvent{std::move(*refill), top.source_idx});
    return std::move(top.event);
}

std::vector<InstrumentSpec> MergedDataSource::get_instrument_infos() {
    std::vector<InstrumentSpec> result;
    for (auto &src : _sources) {
        for (auto &spec : src->get_instrument_infos()) {
            bool dup = std::any_of(result.begin(), result.end(),
                [&](const InstrumentSpec &s){ return s.name == spec.name; });
            if (!dup) result.push_back(std::move(spec));
        }
    }
    return result;
}

} // namespace quarkbot
```

- [ ] **Step 5: Build and run**

```bash
cmake --build build/ --target tests_merged_source_test.cpp && ./build/tests/tests_merged_source_test.cpp
```

Expected: "All merged source tests passed", exit 0.

- [ ] **Step 6: Run all tests**

```bash
cmake --build build/ && ctest --test-dir build/ -V 2>&1 | tail -20
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/impl/merged_data_source.hpp src/impl/merged_data_source.cpp \
        src/tests/merged_source_test.cpp
git commit -m "feat: add MergedDataSource with k-way chronological merge"
```

---

## Self-Review Checklist

- [x] `InstrumentSpec` defined in `backtest_data_source.hpp` — Task 1 ✓
- [x] `get_instrument_infos()` virtual default on `IBacktestDataSource` — Task 1 ✓
- [x] `IExchange::create_currency` added + `SimExchange` override — Task 1 ✓
- [x] `InstrumentSpec::resolve()` implemented — Task 2 ✓
- [x] INI parsing with all required/optional fields — Task 2 ✓
- [x] gz file open/close lifecycle — Task 2 ✓
- [x] `read_line()` handles partial gzgets buffer — Task 2 ✓
- [x] Tardis trades column detection + row parsing — Task 3 ✓
- [x] Tardis quotes column detection + row parsing — Task 4 ✓
- [x] Nanosecond timestamp conversion — Tasks 3/4 ✓
- [x] `MergedDataSource` k-way min-heap — Task 5 ✓
- [x] `get_instrument_infos()` deduplication by name — Task 5 ✓
- [x] CMake: zlib dependency, new source files, test targets — Task 2 ✓
- [x] All tests use `check.h` macros consistent with project — Tasks 1/2/5 ✓
