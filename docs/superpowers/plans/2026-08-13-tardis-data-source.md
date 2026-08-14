# Tardis Data Source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing Tardis trades/quotes readers usable from an INI configuration through `tardis.trades=` / `tardis.quotes=` keys, and correct them to read the format tardis.dev actually publishes.

**Architecture:** No new component. `src/quarkbot/tardis/tardis_data_source.{hpp,cpp}` is corrected in place (microsecond timestamps, `snake_case` columns, symbol from the file's own columns, move constructor, construction-time header validation, per-row errors) and `SourceCollector::walk` in `src/quarkbot/backtest/config_datasource.cpp` gains a `tardis.*` dispatch branch. Truncated copies of three real exports become committed test fixtures.

**Tech Stack:** C++23, zlib (`gzopen`/`gzgets`), `Decimal` (`include/quarkbot/decimal.hpp`), `string_lookup<Side>` (`include/quarkbot/types.hpp`), the `check.h` macro test harness.

**Spec:** `docs/superpowers/specs/2026-08-13-tardis-data-source-design.md`

## Global Constraints

- Build with g++-14. `build/` is already configured with `/usr/bin/g++-14`; the default `g++` on this machine is 13.3 and **cannot** compile this repo (it fails in `include/quarkbot/types.hpp`). To configure a fresh build dir: `cmake -DCMAKE_CXX_COMPILER=g++-14 -S . -B build`.
- Build: `cmake --build build -j$(nproc)`. Full suite: `ctest --test-dir build`. Single binary: `./build/tests/test_tardis_source_test`.
- `CHECK_EXCEPTION` takes the exception type **first**: `CHECK_EXCEPTION(std::runtime_error, expr)`. Same for `CHECK_EXCEPTION_EXPR(type, var, test_expr, expr)`.
- `CHECK_EQUAL(a,b)` streams both sides with `operator<<`, so it only works on streamable types. For `std::chrono::system_clock::time_point` and `Decimal` comparisons use `CHECK(a == b)`.
- Financial values use `Decimal` from `quarkbot/decimal.hpp`, never `double`.
- Header include style inside the impl tree is `#include "quarkbot/<component>/<file>.hpp"` (the impl include root is `src/`).
- Event time is always `local_timestamp`, in **microseconds**. The `timestamp` column is never read and is never required.
- The symbol reported on events is always `exchange + ":" + symbol` taken from the file's own columns.
- Do not add any `tardis.*` option keys. `tardis.trades` and `tardis.quotes` are data keys only.
- The full exports in `data/` are 113 MB and are gitignored. Never `git add` them.

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `include/quarkbot/decimal.hpp` | replace 4 bare `throw "..."` with `std::runtime_error` | 1 |
| `src/tests/decimal_test.cpp` | assert `from_string` throws `std::runtime_error` | 1 |
| `src/quarkbot/tardis/tardis_data_source.hpp` | class declarations | 2–7 |
| `src/quarkbot/tardis/tardis_data_source.cpp` | gzip reading, header mapping, row → event | 2–7 |
| `src/tests/data/*.csv.gz` | 3 truncated real exports (committed fixtures) | 3 |
| `src/tests/tardis_source_test.cpp` | unit + real-fixture regression | 2–7 |
| `src/quarkbot/backtest/config_datasource.cpp` | `tardis.*` dispatch | 8 |
| `src/tests/config_datasource_test.cpp` | configuration-level tests | 8 |
| `src/quarkbot/backtest/config_datasource.hpp` | doc comment for the new keys | 9 |
| `CLAUDE.md` | correct the stale `QUARKBOT_TARDIS` claim | 9 |
| `src/quarkbot/algoseek/algoseek_data_source.hpp` | comment referring to the bare throw | 9 |

## Reference: the real export format

Verified against the three files in `data/`. Every fixture and synthetic test row must use these headers.

```
bitmex_trades_2020-04-01_XBTUSD.csv.gz
exchange,symbol,timestamp,local_timestamp,id,side,price,amount
bitmex,XBTUSD,1585699202957000,1585699203089980,d202810a-ec78-0d65-8652-42c8168f127c,buy,6425.5,12

huobi-dm-swap_quotes_2020-04-01_BTC-USD.csv.gz  (and book_ticker, identical schema)
exchange,symbol,timestamp,local_timestamp,ask_amount,ask_price,bid_price,bid_amount
huobi-dm-swap,BTC-USD,1585699201147000,1585699201270777,86,6423,6422.9,112
```

Note the quote columns are mirrored outside-in: `ask_amount,ask_price,bid_price,bid_amount`.

---

### Task 1: `Decimal` throws `std::runtime_error`

`Decimal` throws bare string literals, which `catch (const std::exception &)` does not catch and which terminate the process with no message when uncaught. Every current workaround in the tree uses `catch (...)`, so replacing the thrown type breaks nothing.

**Files:**
- Modify: `include/quarkbot/decimal.hpp:169`, `:195`, `:220`, `:362`
- Test: `src/tests/decimal_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `Decimal::from_string(std::string_view)` and the `scaleb10`/`divide_pow10`/`modulo_pow10` helpers throw `std::runtime_error` instead of `const char *`. Later tasks catch `const std::exception &`.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/decimal_test.cpp`, and add `test_from_string_errors();` as the first line of `main()`:

```cpp
#include <stdexcept>

static void test_from_string_errors() {
    using namespace quarkbot;
    // a bare `throw "..."` is not caught by this handler, so this asserts the type
    CHECK_EXCEPTION(std::runtime_error, Decimal::from_string("12.5x"));
    CHECK_EXCEPTION(std::runtime_error, Decimal::from_string("not a number"));
    // valid literals must keep working, including at compile time
    static_assert(Decimal::from_string("6425.5") == 6425.5_dec);
    CHECK(Decimal::from_string("-0.001") == Decimal::from_string("-0.001"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_decimal_test && ./build/tests/test_decimal_test`
Expected: the process aborts with `terminate called after throwing an instance of 'char const*'` — the bare literal escapes the `catch (const std::runtime_error &)` handler. Exit code is not 0 and no `Passed:` line for the first check appears.

- [ ] **Step 3: Write minimal implementation**

In `include/quarkbot/decimal.hpp`, replace all four bare throws. Lines 169, 195 and 220 (three `switch` defaults in `divide_pow10`, `modulo_pow10` and `scaleb10`):

```cpp
            default: throw std::runtime_error("Decimal: exponent out of range");
```

Line 362, in `from_string`:

```cpp
        if (iter != e) throw std::runtime_error("Decimal: invalid number format");
```

`<stdexcept>` is already included at line 14; do not add it again. These are `inline constexpr` functions, which may contain a `throw` that is never constant-evaluated — valid literals such as `6425.5_dec` do not reach it.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build`
Expected: all tests pass. Build the **whole** suite, not just the decimal test: `decimal.hpp` is included by nearly every header and `operator""_dec` (`decimal.hpp:689`) calls `from_string` during constant evaluation, so a mistake here breaks compilation far from the edit.

- [ ] **Step 5: Commit**

```bash
git add include/quarkbot/decimal.hpp src/tests/decimal_test.cpp
git commit -m "fix(decimal): throw std::runtime_error instead of a bare string literal"
```

---

### Task 2: Move-constructible sources

`TardisCsvDataSource` declares a destructor (suppressing the implicit move constructor) and deletes the copy constructor, so it is neither copyable nor movable and cannot be stored in `BacktestDataSource`, which is a `std::move_only_function`.

**Files:**
- Modify: `src/quarkbot/tardis/tardis_data_source.hpp:11-25`, `src/quarkbot/tardis/tardis_data_source.cpp`
- Test: `src/tests/tardis_source_test.cpp`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `TardisCsvDataSource(TardisCsvDataSource &&) noexcept`. `TardisTradesDataSource` and `TardisQuotesDataSource` get implicit move constructors and satisfy `BacktestDataSourceType`.

- [ ] **Step 1: Write the failing test**

Restructure `src/tests/tardis_source_test.cpp` so `main()` calls named functions, and add this one. Keep the existing trades/quotes bodies as `test_trades()` / `test_quotes()` for now — Task 3 rewrites their data.

```cpp
#include "quarkbot/abstract/backtest_data_source.hpp"
#include <type_traits>

static void test_movable() {
    using namespace quarkbot;
    static_assert(std::is_move_constructible_v<TardisTradesDataSource>);
    static_assert(std::is_move_constructible_v<TardisQuotesDataSource>);
    static_assert(BacktestDataSourceType<TardisTradesDataSource>);

    write_gz("/tmp/test_tardis_movable.csv.gz", TRADES_CSV);
    // the point of the move constructor: a source has to survive being put here
    BacktestDataSource ds = TardisTradesDataSource("BTCUSDT", "/tmp/test_tardis_movable.csv.gz");
    BacktestEvent ev;
    CHECK(ds(ev));
    CHECK_EQUAL(ev.symbol, std::string("BTCUSDT"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_tardis_source_test`
Expected: FAIL at compile time — `static_assert` on `is_move_constructible_v` fires, and the `BacktestDataSource ds = ...` line reports that `std::move_only_function` cannot be constructed from the type.

- [ ] **Step 3: Write minimal implementation**

In `tardis_data_source.hpp`, add to the public section of `TardisCsvDataSource` after the deleted copy assignment:

```cpp
    TardisCsvDataSource(TardisCsvDataSource &&other) noexcept;
```

Add `#include <utility>` to `tardis_data_source.cpp` and define it next to the destructor:

```cpp
TardisCsvDataSource::TardisCsvDataSource(TardisCsvDataSource &&other) noexcept
    :_instrument(std::move(other._instrument))
    ,_gz(std::exchange(other._gz, nullptr)) {}
```

Leaving `other._gz` null is what makes the moved-from destructor a no-op instead of a double `gzclose`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) --target test_tardis_source_test && ./build/tests/test_tardis_source_test`
Expected: PASS, including `Passed: ev.symbol==std::string("BTCUSDT")`.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/tardis/tardis_data_source.hpp src/quarkbot/tardis/tardis_data_source.cpp src/tests/tardis_source_test.cpp
git commit -m "fix(tardis): make the CSV sources move-constructible"
```

---

### Task 3: Read the format Tardis actually publishes

The single largest correction. The reader looks for camelCase column names from the Tardis JSON API (`bidPrice`, `askSize`, `localTimestamp`), none of which occurs in a CSV export, and reads timestamps as nanoseconds instead of microseconds. Column mapping also moves into the constructor so a missing column fails at startup instead of indexing out of bounds.

**Files:**
- Create: `src/tests/data/bitmex_trades_2020-04-01_XBTUSD.csv.gz`
- Create: `src/tests/data/huobi-dm-swap_quotes_2020-04-01_BTC-USD.csv.gz`
- Create: `src/tests/data/binance-futures_book_ticker_2024-04-01_ETHUSDT.csv.gz`
- Modify: `src/quarkbot/tardis/tardis_data_source.hpp`, `src/quarkbot/tardis/tardis_data_source.cpp`
- Test: `src/tests/tardis_source_test.cpp`

**Interfaces:**
- Consumes: the move constructor from Task 2.
- Produces:
  - `TardisCsvDataSource::optional_column(std::string_view name) const -> int` (−1 when absent)
  - `TardisCsvDataSource::require_column(std::string_view name) -> int` (records the name when absent)
  - `TardisCsvDataSource::check_columns() const -> void` (throws `std::runtime_error` listing every recorded name)
  - Both derived constructors map their columns and call `check_columns()`; `operator()` no longer parses a header.

- [ ] **Step 1: Create the fixtures**

Header plus the first 2000 data rows of each export, recompressed. Total is 96 KB, the same order as the Algoseek fixtures already in that directory.

```bash
for f in bitmex_trades_2020-04-01_XBTUSD \
         huobi-dm-swap_quotes_2020-04-01_BTC-USD \
         binance-futures_book_ticker_2024-04-01_ETHUSDT; do
  zcat "data/$f.csv.gz" | head -2001 | gzip -9 > "src/tests/data/$f.csv.gz"
done
du -sh src/tests/data
```

Expected: `96K` (or within a few KB).

- [ ] **Step 2: Write the failing test**

Replace the bodies of `test_trades()` and `test_quotes()` in `src/tests/tardis_source_test.cpp` and add the fixture regression. `TEST_DATA_PATH` is defined by `src/tests/CMakeLists.txt` and points at `src/tests/data`.

```cpp
#include <filesystem>

///parse "2020-04-01 00:00:03.089980" into a time_point
static std::chrono::system_clock::time_point mk_utc(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::chrono::sys_time<std::chrono::microseconds> tp;
    in >> std::chrono::parse("%F %T", tp);
    if (in.fail()) { std::cerr << "bad test instant: " << text << std::endl; exit(1); }
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(tp);
}

// the real CSV header, with local_timestamp deliberately 1 second after
// timestamp so that reading the wrong column fails the assertions
static const std::string_view TRADES_CSV =
    "exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n"
    "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,buy,6425.5,12\n"
    "bitmex,XBTUSD,1585699203957000,1585699204957000,bbb,sell,6425,150\n";

static const std::string_view QUOTES_CSV =
    "exchange,symbol,timestamp,local_timestamp,ask_amount,ask_price,bid_price,bid_amount\n"
    "huobi-dm-swap,BTC-USD,1585699201147000,1585699202147000,86,6423,6422.9,112\n"
    "huobi-dm-swap,BTC-USD,1585699202147000,1585699203147000,88,6424,6423.9,114\n";

static void test_trades() {
    using namespace quarkbot;
    write_gz("/tmp/test_tardis_trades.csv.gz", TRADES_CSV);
    TardisTradesDataSource src("BTCUSDT", "/tmp/test_tardis_trades.csv.gz");

    BacktestEvent e1;
    CHECK(src(e1));
    CHECK(std::holds_alternative<Trade>(e1.data));
    // microseconds, and local_timestamp rather than timestamp
    CHECK(e1.time == mk_utc("2020-04-01 00:00:03.957000"));
    CHECK(std::get<Trade>(e1.data).price == Decimal::from_string("6425.5"));
    CHECK(std::get<Trade>(e1.data).size == Decimal::from_string("12"));

    BacktestEvent e2;
    CHECK(src(e2));
    CHECK(e2.time == mk_utc("2020-04-01 00:00:04.957000"));
    BacktestEvent e3;
    CHECK(!src(e3));
}

static void test_quotes() {
    using namespace quarkbot;
    write_gz("/tmp/test_tardis_quotes.csv.gz", QUOTES_CSV);
    TardisQuotesDataSource src("BTC-USD", "/tmp/test_tardis_quotes.csv.gz");

    BacktestEvent q1;
    CHECK(src(q1));
    CHECK(std::holds_alternative<Quote>(q1.data));
    CHECK(q1.time == mk_utc("2020-04-01 00:00:02.147000"));
    auto &quote = std::get<Quote>(q1.data);
    // the columns are mirrored outside-in; bid and ask must not be transposed
    CHECK(quote.bid == Decimal::from_string("6422.9"));
    CHECK(quote.bid_size == Decimal::from_string("112"));
    CHECK(quote.ask == Decimal::from_string("6423"));
    CHECK(quote.ask_size == Decimal::from_string("86"));
}

static void test_construction_errors() {
    using namespace quarkbot;
    // amount is absent: today this indexes cols[size_t(-1)], it must throw instead
    write_gz("/tmp/test_tardis_nocol.csv.gz",
        "exchange,symbol,timestamp,local_timestamp,id,side,price\n"
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,buy,6425.5\n");
    CHECK_EXCEPTION_EXPR(std::runtime_error, e,
        std::string_view(e.what()).find("amount") != std::string_view::npos,
        TardisTradesDataSource src("XBTUSD", "/tmp/test_tardis_nocol.csv.gz"));

    // a nonexistent file fails when the source is constructed, naming the path
    CHECK_EXCEPTION_EXPR(std::runtime_error, e,
        std::string_view(e.what()).find("does_not_exist_tardis") != std::string_view::npos,
        TardisTradesDataSource src("XBTUSD", "/tmp/does_not_exist_tardis.csv.gz"));
}

///a valid header with no data rows is an empty source, not an error
static void test_header_only() {
    using namespace quarkbot;
    write_gz("/tmp/test_tardis_headeronly.csv.gz",
        "exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n");
    TardisTradesDataSource src("XBTUSD", "/tmp/test_tardis_headeronly.csv.gz");
    BacktestEvent ev;
    CHECK(!src(ev));
}

static void test_real_exports() {
    using namespace quarkbot;
    const std::filesystem::path dir = TEST_DATA_PATH;

    // bitmex trades, 2000 rows
    {
        TardisTradesDataSource src("XBTUSD",
            dir/"bitmex_trades_2020-04-01_XBTUSD.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK(ev.time == mk_utc("2020-04-01 00:00:03.089980"));
        CHECK(std::get<Trade>(ev.data).price == Decimal::from_string("6425.5"));
        std::size_t n = 1;
        while (src(ev)) ++n;
        CHECK_EQUAL(n, std::size_t(2000));
        CHECK(ev.time == mk_utc("2020-04-01 00:00:21.951810"));
        CHECK(std::get<Trade>(ev.data).price == Decimal::from_string("6428.5"));
        CHECK(std::get<Trade>(ev.data).size == Decimal::from_string("2000"));
    }
    // huobi quotes, 2000 rows
    {
        TardisQuotesDataSource src("BTC-USD",
            dir/"huobi-dm-swap_quotes_2020-04-01_BTC-USD.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK(ev.time == mk_utc("2020-04-01 00:00:01.270777"));
        CHECK(std::get<Quote>(ev.data).bid == Decimal::from_string("6422.9"));
        CHECK(std::get<Quote>(ev.data).ask == Decimal::from_string("6423"));
        std::size_t n = 1;
        while (src(ev)) ++n;
        CHECK_EQUAL(n, std::size_t(2000));
        CHECK(ev.time == mk_utc("2020-04-01 00:03:53.783935"));
        CHECK(std::get<Quote>(ev.data).bid == Decimal::from_string("6432.4"));
        CHECK(std::get<Quote>(ev.data).ask == Decimal::from_string("6432.5"));
    }
    // binance-futures book_ticker: same schema as quotes, read by the same class
    {
        TardisQuotesDataSource src("ETHUSDT",
            dir/"binance-futures_book_ticker_2024-04-01_ETHUSDT.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK(ev.time == mk_utc("2024-04-01 00:00:00.011559"));
        CHECK(std::get<Quote>(ev.data).bid == Decimal::from_string("3648.79"));
        CHECK(std::get<Quote>(ev.data).ask == Decimal::from_string("3648.8"));
        std::size_t n = 1;
        while (src(ev)) ++n;
        CHECK_EQUAL(n, std::size_t(2000));
        CHECK(ev.time == mk_utc("2024-04-01 00:00:14.408782"));
        CHECK(std::get<Quote>(ev.data).bid == Decimal::from_string("3646.29"));
    }
}
```

Add `test_construction_errors(); test_header_only(); test_real_exports();` to `main()`. Add `#include <sstream>` and `#include <stdexcept>` if not already present.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_tardis_source_test && ./build/tests/test_tardis_source_test`
Expected: FAIL. `test_trades` fails on the timestamp assertion (nanosecond parsing puts the event around 1970 and it reads `timestamp` not `local_timestamp`); `test_quotes` and the quote fixtures crash or produce garbage, because no bid/ask column name matches and `cols[static_cast<std::size_t>(-1)]` is an out-of-bounds access; the missing-column case fails because nothing throws. The nonexistent-file and header-only cases already pass — they assert behaviour that must survive moving the header read into the constructor, which is exactly what could break it.

- [ ] **Step 4: Write minimal implementation**

`tardis_data_source.hpp` — add `#include <string_view>`, `#include <vector>`, and to the protected section of `TardisCsvDataSource`:

```cpp
    ///index of the named header column, -1 when the file has no such column
    int optional_column(std::string_view name) const;
    ///like optional_column, but records a missing name for check_columns()
    int require_column(std::string_view name);
    ///throw naming every column require_column() did not find
    void check_columns() const;
    const std::filesystem::path &path() const {return _path;}
```

and to the private section:

```cpp
    std::filesystem::path _path;
    std::vector<std::string> _header;
    std::string _missing;
```

Replace both derived classes' member blocks and give each a constructor:

```cpp
class TardisTradesDataSource : public TardisCsvDataSource {
public:
    TardisTradesDataSource(std::string instrument, std::filesystem::path csv_gz_path);
    bool operator()(BacktestEvent &ev);
private:
    int _col_local_timestamp, _col_price, _col_amount;
    std::size_t _min_cols;
};

class TardisQuotesDataSource : public TardisCsvDataSource {
public:
    TardisQuotesDataSource(std::string instrument, std::filesystem::path csv_gz_path);
    bool operator()(BacktestEvent &ev);
private:
    int _col_local_timestamp, _col_bid_price, _col_bid_amount, _col_ask_price, _col_ask_amount;
    std::size_t _min_cols;
};
```

`tardis_data_source.cpp` — replace `parse_ns_timestamp` with:

```cpp
// Parse a Tardis microsecond unix timestamp to system_clock::time_point
static std::chrono::system_clock::time_point parse_us_timestamp(std::string_view s) {
    long long us = 0;
    for (char c : s) {
        if (c < '0' || c > '9') break;
        us = us * 10 + (c - '0');
    }
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::microseconds(us)));
}
```

Store the path and read the header in the base constructor:

```cpp
TardisCsvDataSource::TardisCsvDataSource(std::string instrument, std::filesystem::path csv_gz_path)
    : _instrument(std::move(instrument)), _path(std::move(csv_gz_path)) {
    #ifdef _WIN32
    _gz = reinterpret_cast<gzFile_s *>(gzopen_w(_path.c_str(), "rb"));
    #else
    _gz = reinterpret_cast<gzFile_s *>(gzopen(_path.c_str(), "rb"));
    #endif
    if (!_gz) throw std::runtime_error("Cannot open gz file: " + _path.string());
    std::string header;
    if (read_line(header)) {
        for (auto col: split_csv(header)) _header.emplace_back(col);
    }
}
```

Extend the move constructor with the three new members:

```cpp
TardisCsvDataSource::TardisCsvDataSource(TardisCsvDataSource &&other) noexcept
    :_instrument(std::move(other._instrument))
    ,_path(std::move(other._path))
    ,_gz(std::exchange(other._gz, nullptr))
    ,_header(std::move(other._header))
    ,_missing(std::move(other._missing)) {}
```

Add the column helpers:

```cpp
int TardisCsvDataSource::optional_column(std::string_view name) const {
    for (std::size_t i = 0; i < _header.size(); ++i)
        if (_header[i] == name) return static_cast<int>(i);
    return -1;
}

int TardisCsvDataSource::require_column(std::string_view name) {
    int i = optional_column(name);
    if (i < 0) {
        if (!_missing.empty()) _missing.append(", ");
        _missing.append(name);
    }
    return i;
}

void TardisCsvDataSource::check_columns() const {
    if (!_missing.empty())
        throw std::runtime_error(std::format(
            "Tardis source: missing column(s) {} in file {}", _missing, _path.string()));
}
```

Add `#include <format>`. Now the derived constructors, which replace the lazy header parsing:

```cpp
TardisTradesDataSource::TardisTradesDataSource(std::string instrument, std::filesystem::path p)
    :TardisCsvDataSource(std::move(instrument), std::move(p))
{
    _col_local_timestamp = require_column("local_timestamp");
    _col_price = require_column("price");
    _col_amount = require_column("amount");
    check_columns();
    _min_cols = static_cast<std::size_t>(
        std::max({_col_local_timestamp, _col_price, _col_amount})) + 1;
}

TardisQuotesDataSource::TardisQuotesDataSource(std::string instrument, std::filesystem::path p)
    :TardisCsvDataSource(std::move(instrument), std::move(p))
{
    _col_local_timestamp = require_column("local_timestamp");
    _col_bid_price  = require_column("bid_price");
    _col_bid_amount = require_column("bid_amount");
    _col_ask_price  = require_column("ask_price");
    _col_ask_amount = require_column("ask_amount");
    check_columns();
    _min_cols = static_cast<std::size_t>(std::max({_col_local_timestamp,
        _col_bid_price, _col_bid_amount, _col_ask_price, _col_ask_amount})) + 1;
}
```

Rewrite both `operator()` bodies with the header block removed:

```cpp
bool TardisTradesDataSource::operator()(BacktestEvent &ev) {
    std::string line;
    while (read_line(line)) {
        if (line.empty()) continue;
        auto cols = split_csv(line);
        if (cols.size() < _min_cols) continue;

        auto tp = parse_us_timestamp(cols[static_cast<std::size_t>(_col_local_timestamp)]);
        Decimal price, amount;
        try {
            price  = Decimal::from_string(cols[static_cast<std::size_t>(_col_price)]);
            amount = Decimal::from_string(cols[static_cast<std::size_t>(_col_amount)]);
        } catch (...) { continue; }

        Trade trade;
        trade.price = price;
        trade.size  = amount;
        trade.time  = tp;
        ev.symbol = instrument();
        ev.time = tp;
        ev.data = trade;
        return true;
    }
    return false;
}

bool TardisQuotesDataSource::operator()(BacktestEvent &ev) {
    std::string line;
    while (read_line(line)) {
        if (line.empty()) continue;
        auto cols = split_csv(line);
        if (cols.size() < _min_cols) continue;

        auto tp = parse_us_timestamp(cols[static_cast<std::size_t>(_col_local_timestamp)]);
        Decimal bid, bid_size, ask, ask_size;
        try {
            bid      = Decimal::from_string(cols[static_cast<std::size_t>(_col_bid_price)]);
            bid_size = Decimal::from_string(cols[static_cast<std::size_t>(_col_bid_amount)]);
            ask      = Decimal::from_string(cols[static_cast<std::size_t>(_col_ask_price)]);
            ask_size = Decimal::from_string(cols[static_cast<std::size_t>(_col_ask_amount)]);
        } catch (...) { continue; }

        Quote quote;
        quote.bid      = bid;
        quote.bid_size = bid_size;
        quote.ask      = ask;
        quote.ask_size = ask_size;
        quote.time     = tp;
        ev.symbol = instrument();
        ev.time = tp;
        ev.data = quote;
        return true;
    }
    return false;
}
```

The `catch (...) { continue; }` and the `_min_cols` skip stay for now; Task 6 turns both into errors.

Move `split_csv` above the constructor so the base can call it, or forward-declare it.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build`
Expected: all pass, including the three fixture blocks with 2000 events each.

- [ ] **Step 6: Commit**

```bash
git add src/quarkbot/tardis/ src/tests/tardis_source_test.cpp src/tests/data/
git commit -m "fix(tardis): read the real CSV format - microseconds, local_timestamp, snake_case columns"
```

---

### Task 4: Symbol from the file's own columns

The `instrument` constructor parameter is redundant: every row carries `exchange` and `symbol`.

**Files:**
- Modify: `src/quarkbot/tardis/tardis_data_source.hpp`, `src/quarkbot/tardis/tardis_data_source.cpp`
- Test: `src/tests/tardis_source_test.cpp`

**Interfaces:**
- Consumes: `require_column` / `check_columns` from Task 3.
- Produces: `TardisTradesDataSource(std::filesystem::path)` and `TardisQuotesDataSource(std::filesystem::path)` — one argument. `TardisCsvDataSource::row_symbol(std::string_view exchange, std::string_view symbol) -> const std::string &`, which caches `exchange:symbol` from the first row. `instrument()` and the `_instrument` member are gone.

- [ ] **Step 1: Write the failing test**

Change every construction in `src/tests/tardis_source_test.cpp` to drop the first argument, and assert the symbol. In `test_trades()`:

```cpp
    TardisTradesDataSource src("/tmp/test_tardis_trades.csv.gz");
    ...
    CHECK_EQUAL(e1.symbol, std::string("bitmex:XBTUSD"));
```

In `test_quotes()`:

```cpp
    TardisQuotesDataSource src("/tmp/test_tardis_quotes.csv.gz");
    ...
    CHECK_EQUAL(q1.symbol, std::string("huobi-dm-swap:BTC-USD"));
```

In `test_movable()`, `test_construction_errors()`, `test_header_only()` and each block of `test_real_exports()`, drop the first argument and add:

```cpp
    // test_movable, over TRADES_CSV
    CHECK_EQUAL(ev.symbol, std::string("bitmex:XBTUSD"));
    // test_real_exports, per block
    CHECK_EQUAL(ev.symbol, std::string("bitmex:XBTUSD"));
    CHECK_EQUAL(ev.symbol, std::string("huobi-dm-swap:BTC-USD"));
    CHECK_EQUAL(ev.symbol, std::string("binance-futures:ETHUSDT"));
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_tardis_source_test`
Expected: FAIL at compile time — no constructor taking one argument.

- [ ] **Step 3: Write minimal implementation**

`tardis_data_source.hpp` — in `TardisCsvDataSource` replace `explicit`-less two-arg constructor and `instrument()`:

```cpp
    explicit TardisCsvDataSource(std::filesystem::path csv_gz_path);
```

```cpp
    ///symbol of the current row as exchange:symbol, cached from the first row
    const std::string &row_symbol(std::string_view exchange, std::string_view symbol);
```

Delete the `_instrument` member and add:

```cpp
    std::string _symbol;
```

Both derived constructors become one-argument, and each maps two more columns:

```cpp
class TardisTradesDataSource : public TardisCsvDataSource {
public:
    explicit TardisTradesDataSource(std::filesystem::path csv_gz_path);
    ...
private:
    int _col_exchange, _col_symbol, _col_local_timestamp, _col_price, _col_amount;
```

`tardis_data_source.cpp` — base constructor drops the parameter, move constructor swaps `_instrument` for `_symbol`:

```cpp
TardisCsvDataSource::TardisCsvDataSource(std::filesystem::path csv_gz_path)
    : _path(std::move(csv_gz_path)) {
```

```cpp
    ,_symbol(std::move(other._symbol))
```

Add:

```cpp
const std::string &TardisCsvDataSource::row_symbol(
        std::string_view exchange, std::string_view symbol) {
    if (_symbol.empty()) {
        _symbol.append(exchange).append(":").append(symbol);
    }
    return _symbol;
}
```

Each derived constructor adds the two columns before `check_columns()`:

```cpp
    _col_exchange = require_column("exchange");
    _col_symbol = require_column("symbol");
```

and includes them in the `std::max({...})` for `_min_cols`. In both `operator()` bodies replace `ev.symbol = instrument();` with:

```cpp
        ev.symbol = row_symbol(cols[static_cast<std::size_t>(_col_exchange)],
                               cols[static_cast<std::size_t>(_col_symbol)]);
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build`
Expected: all pass, with `bitmex:XBTUSD`, `huobi-dm-swap:BTC-USD` and `binance-futures:ETHUSDT` in the output.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/tardis/ src/tests/tardis_source_test.cpp
git commit -m "feat(tardis): take the symbol from the file's exchange and symbol columns"
```

---

### Task 5: Map the taker side on trades

**Files:**
- Modify: `src/quarkbot/tardis/tardis_data_source.hpp`, `src/quarkbot/tardis/tardis_data_source.cpp`
- Test: `src/tests/tardis_source_test.cpp`

**Interfaces:**
- Consumes: the one-argument constructors from Task 4.
- Produces: `Trade::side` is set from the `side` column. No new public names.

- [ ] **Step 1: Write the failing test**

`TRADES_CSV` already has `buy` on row 1 and `sell` on row 2. Add to `test_trades()`:

```cpp
    CHECK(std::get<Trade>(e1.data).side == Side::buy);
    CHECK(std::get<Trade>(e2.data).side == Side::sell);
```

To `test_real_exports()`, in the bitmex block, after the first event and after the loop:

```cpp
    CHECK(std::get<Trade>(ev.data).side == Side::buy);   // both boundary rows are buys
```

And a case for a file with no `side` column, since quotes exports have none and `Side::undetermined` is valid:

```cpp
static void test_side_optional() {
    using namespace quarkbot;
    write_gz("/tmp/test_tardis_noside.csv.gz",
        "exchange,symbol,timestamp,local_timestamp,price,amount\n"
        "bitmex,XBTUSD,1585699202957000,1585699203957000,6425.5,12\n");
    TardisTradesDataSource src("/tmp/test_tardis_noside.csv.gz");
    BacktestEvent ev;
    CHECK(src(ev));
    CHECK(std::get<Trade>(ev.data).side == Side::undetermined);
}
```

Add `test_side_optional();` to `main()`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_tardis_source_test && ./build/tests/test_tardis_source_test`
Expected: FAIL on `side == Side::buy` — the column is never read, so the side stays `Side::undetermined`.

- [ ] **Step 3: Write minimal implementation**

Add `int _col_side;` to `TardisTradesDataSource`'s private members. In its constructor, after the required columns:

```cpp
    _col_side = optional_column("side");
```

`_col_side` must **not** go into `require_column` or into `_min_cols` — a trades file without it is valid. Guard the access on the column being present and long enough. In `operator()`, after `trade.time = tp;`:

```cpp
        if (_col_side >= 0 && cols.size() > static_cast<std::size_t>(_col_side)) {
            auto s = string_lookup<Side>(cols[static_cast<std::size_t>(_col_side)]);
            trade.side = s.value_or(Side::undetermined);
        }
```

`string_lookup<Side>` is bidirectional (`include/quarkbot/utils/lookup.hpp:27`), so calling it with a `std::string_view` returns `std::optional<Side>`. Task 6 turns an unrecognised value into an error instead of `undetermined`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/tardis/ src/tests/tardis_source_test.cpp
git commit -m "feat(tardis): map the trade side column to Trade::side"
```

---

### Task 6: Row errors instead of silent skips

Four silent paths remain: a short row, an unparseable number, an unrecognised `side`, and — worst of the four — an unparseable timestamp. Each drops or corrupts data with no counter and no message.

`parse_us_timestamp` as written in Task 3 stops at the first non-digit and returns what it accumulated, so an empty or non-numeric `local_timestamp` yields epoch 0 and the row is **emitted** at that time rather than rejected; the parse sits outside the `try`/`catch` that guards the `Decimal` conversions, so nothing catches it. It also accumulates digits without a length bound, which is signed overflow on a long field, and `duration_cast` to `system_clock::duration` multiplies by 1000, which overflows for a large value. The Algoseek reader already guards exactly these cases (`algoseek_data_source.cpp:135-172`, commit `6b79470`); this brings Tardis in line.

**Files:**
- Modify: `src/quarkbot/tardis/tardis_data_source.hpp`, `src/quarkbot/tardis/tardis_data_source.cpp`
- Test: `src/tests/tardis_source_test.cpp`

**Interfaces:**
- Consumes: everything from Tasks 3–5, and `Decimal::from_string` throwing `std::runtime_error` from Task 1.
- Produces: `TardisCsvDataSource::row_error(std::string_view message) const` marked `[[noreturn]]`, `parse_decimal(std::string_view value, std::string_view column) const -> Decimal`, and `parse_us_timestamp(std::string_view value, std::string_view column) const -> std::chrono::system_clock::time_point` as a member replacing the Task 3 file-static free function. A row counter `_line` where the header is row 1.

- [ ] **Step 1: Write the failing test**

```cpp
static void test_row_errors() {
    using namespace quarkbot;
    const std::string_view head =
        "exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n";

    // a non-numeric price names the file, the row and the column
    write_gz("/tmp/test_tardis_badprice.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,buy,abc,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_badprice.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("price") != std::string_view::npos
            && std::string_view(e.what()).find("badprice") != std::string_view::npos,
            src(ev));
    }
    // a short row
    write_gz("/tmp/test_tardis_short.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699203957000\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_short.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }
    // an unrecognised side
    write_gz("/tmp/test_tardis_badside.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,sideways,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_badside.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("sideways") != std::string_view::npos,
            src(ev));
    }
    // a non-numeric timestamp must not silently become epoch 0
    write_gz("/tmp/test_tardis_badtime.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,notanumber,aaa,buy,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_badtime.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("local_timestamp") != std::string_view::npos,
            src(ev));
    }
    // an empty timestamp, same reasoning
    write_gz("/tmp/test_tardis_emptytime.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,,aaa,buy,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_emptytime.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }
    // a timestamp too large for system_clock::duration: accumulating it would
    // overflow, and duration_cast to nanoseconds multiplies by another 1000
    write_gz("/tmp/test_tardis_hugetime.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,99999999999999999999,aaa,buy,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_hugetime.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }
    // a negative timestamp: std::from_chars accepts a leading '-'
    write_gz("/tmp/test_tardis_negtime.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,-1585699203957000,aaa,buy,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_negtime.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }
    // the row number identifies the offending line; the header is row 1
    write_gz("/tmp/test_tardis_row3.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,buy,6425.5,12\n"
        "bitmex,XBTUSD,1585699203957000,1585699204957000,bbb,buy,xyz,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_row3.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("row 3") != std::string_view::npos,
            src(ev));
    }
}
```

Add `test_row_errors();` to `main()`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_tardis_source_test && ./build/tests/test_tardis_source_test`
Expected: FAIL — each bad row is skipped, `src(ev)` returns `false` without throwing, so `CHECK_EXCEPTION` reports `FAILED: throw std::runtime_error`.

- [ ] **Step 3: Write minimal implementation**

`tardis_data_source.hpp` — protected section:

```cpp
    ///throw a runtime_error naming the file and the row last read
    [[noreturn]] void row_error(std::string_view message) const;
    ///parse a decimal column, turning a parse failure into a row_error
    Decimal parse_decimal(std::string_view value, std::string_view column) const;
    ///parse a microsecond unix timestamp column, rejecting anything but digits
    ///that fit system_clock::duration
    std::chrono::system_clock::time_point parse_us_timestamp(
            std::string_view value, std::string_view column) const;
```

Delete the file-static `parse_us_timestamp` free function that Task 3 added; this member replaces it. Add `#include <charconv>` and `#include <chrono>` to the .cpp.

private section:

```cpp
    ///number of the row last read; the header is row 1
    std::uint64_t _line = 0;
```

Add `#include <cstdint>`. Extend the move constructor with `,_line(other._line)`.

`tardis_data_source.cpp` — count lines in `read_line`, just before each successful `return true` and in the final `return !out.empty();` path. The simplest correct form is to wrap the existing body:

```cpp
bool TardisCsvDataSource::read_line(std::string &out) {
    bool ok = read_line_raw(out);
    if (ok) ++_line;
    return ok;
}
```

renaming the current implementation to `read_line_raw` (declare it private in the header, returning `bool`, same signature).

```cpp
void TardisCsvDataSource::row_error(std::string_view message) const {
    throw std::runtime_error(std::format(
        "Tardis source: {} in file {}, row {}", message, _path.string(), _line));
}

Decimal TardisCsvDataSource::parse_decimal(std::string_view value, std::string_view column) const {
    try {
        return Decimal::from_string(value);
    } catch (const std::exception &) {
        row_error(std::format("column {} is not a number: '{}'", column, value));
    }
}
```

`catch (const std::exception &)` is correct only because Task 1 changed what `Decimal` throws.

Replace the free `parse_us_timestamp` with the guarded member:

```cpp
std::chrono::system_clock::time_point TardisCsvDataSource::parse_us_timestamp(
        std::string_view value, std::string_view column) const {
    long long us = 0;
    auto res = std::from_chars(value.data(), value.data() + value.size(), us);
    if (res.ec != std::errc{} || res.ptr != value.data() + value.size() || us < 0) {
        row_error(std::format(
            "column {} is not a microsecond timestamp: '{}'", column, value));
    }
    //system_clock::duration is nanoseconds here, so duration_cast multiplies by
    //1000; anything above this bound overflows instead of converting
    constexpr long long max_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::duration::max()).count();
    if (us > max_us) {
        row_error(std::format("column {} is out of range: '{}'", column, value));
    }
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::microseconds(us)));
}
```

`std::from_chars` reports overflow as `std::errc::result_out_of_range`, and the `res.ptr` check rejects trailing garbage that the digit loop used to ignore. An empty value fails on `res.ec`. A leading `-` parses, which is why `us < 0` is tested separately.

Both `operator()` bodies now call it with the column name:

```cpp
        auto tp = parse_us_timestamp(
            cols[static_cast<std::size_t>(_col_local_timestamp)], "local_timestamp");
```

In both `operator()` bodies replace the short-row skip:

```cpp
        if (cols.size() < _min_cols) row_error("truncated row");
```

and the try/catch with direct calls. For trades:

```cpp
        Trade trade;
        trade.price = parse_decimal(cols[static_cast<std::size_t>(_col_price)], "price");
        trade.size  = parse_decimal(cols[static_cast<std::size_t>(_col_amount)], "amount");
        trade.time  = tp;
```

For quotes:

```cpp
        Quote quote;
        quote.bid      = parse_decimal(cols[static_cast<std::size_t>(_col_bid_price)], "bid_price");
        quote.bid_size = parse_decimal(cols[static_cast<std::size_t>(_col_bid_amount)], "bid_amount");
        quote.ask      = parse_decimal(cols[static_cast<std::size_t>(_col_ask_price)], "ask_price");
        quote.ask_size = parse_decimal(cols[static_cast<std::size_t>(_col_ask_amount)], "ask_amount");
        quote.time     = tp;
```

And make an unrecognised side an error:

```cpp
        if (_col_side >= 0 && cols.size() > static_cast<std::size_t>(_col_side)) {
            auto raw = cols[static_cast<std::size_t>(_col_side)];
            auto s = string_lookup<Side>(raw);
            if (!s) row_error(std::format("unknown side value: '{}'", raw));
            trade.side = *s;
        }
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build`
Expected: all pass. In particular the 2000-row fixtures must still drain cleanly — a real export must not trip any of the new errors.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/tardis/ src/tests/tardis_source_test.cpp
git commit -m "fix(tardis): fail on malformed rows instead of skipping them silently"
```

---

### Task 7: Reject unordered and mixed-instrument input

`MergedDataSource` is a heap over inputs it assumes are sorted; an unsorted input silently corrupts the merged timeline. A file whose symbol changes mid-way would be folded into one instrument.

**Files:**
- Modify: `src/quarkbot/tardis/tardis_data_source.hpp`, `src/quarkbot/tardis/tardis_data_source.cpp`
- Test: `src/tests/tardis_source_test.cpp`

**Interfaces:**
- Consumes: `row_error` from Task 6, `row_symbol` from Task 4.
- Produces: `TardisCsvDataSource::check_order(std::chrono::system_clock::time_point t)`, which throws on a decreasing timestamp. `row_symbol` now throws when the row's symbol differs from the cached one.

- [ ] **Step 1: Write the failing test**

```cpp
static void test_ordering_and_identity() {
    using namespace quarkbot;
    const std::string_view head =
        "exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n";

    // a decreasing local_timestamp would corrupt the merged timeline
    write_gz("/tmp/test_tardis_unordered.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699204957000,aaa,buy,6425.5,12\n"
        "bitmex,XBTUSD,1585699203957000,1585699203957000,bbb,buy,6426.0,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_unordered.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }
    // an equal timestamp is fine, ticks share a microsecond routinely
    write_gz("/tmp/test_tardis_equal.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,buy,6425.5,12\n"
        "bitmex,XBTUSD,1585699202957000,1585699203957000,bbb,buy,6426.0,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_equal.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK(src(ev));
    }
    // two instruments concatenated into one file
    write_gz("/tmp/test_tardis_twosyms.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,buy,6425.5,12\n"
        "bitmex,ETHUSD,1585699203957000,1585699204957000,bbb,buy,140.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_twosyms.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("ETHUSD") != std::string_view::npos,
            src(ev));
    }
}
```

Add `test_ordering_and_identity();` to `main()`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_tardis_source_test && ./build/tests/test_tardis_source_test`
Expected: FAIL on both `CHECK_EXCEPTION` calls — nothing checks order or identity, so both rows are returned normally.

- [ ] **Step 3: Write minimal implementation**

`tardis_data_source.hpp` — protected section:

```cpp
    ///throw when the timestamp is below the previous row's
    void check_order(std::chrono::system_clock::time_point t);
```

private section:

```cpp
    std::chrono::system_clock::time_point _last_time = {};
```

Add `#include <chrono>`. Extend the move constructor with `,_last_time(other._last_time)`.

`tardis_data_source.cpp`:

```cpp
void TardisCsvDataSource::check_order(std::chrono::system_clock::time_point t) {
    if (t < _last_time) row_error("local_timestamp is lower than on the previous row");
    _last_time = t;
}
```

Extend `row_symbol` to verify instead of only caching. The comparison avoids building a second string per row:

```cpp
const std::string &TardisCsvDataSource::row_symbol(
        std::string_view exchange, std::string_view symbol) {
    if (_symbol.empty()) {
        _symbol.append(exchange).append(":").append(symbol);
    } else if (_symbol.size() != exchange.size() + 1 + symbol.size()
            || !_symbol.starts_with(exchange)
            || _symbol[exchange.size()] != ':'
            || !_symbol.ends_with(symbol)) {
        row_error(std::format("symbol changed from {} to {}:{}", _symbol, exchange, symbol));
    }
    return _symbol;
}
```

In both `operator()` bodies, call `check_order(tp);` on the line after `tp` is parsed.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build`
Expected: all pass. The 2000-row fixtures must still drain — real exports are ordered and single-instrument, so a failure here means the check is wrong, not the data.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/tardis/ src/tests/tardis_source_test.cpp
git commit -m "fix(tardis): reject unordered rows and mixed instruments"
```

---

### Task 8: Wire `tardis.trades` and `tardis.quotes` into the configuration

**Files:**
- Modify: `src/quarkbot/backtest/config_datasource.cpp:71-95` (the data-section key dispatch) and delete `add_tardis` at `:124-126`
- Test: `src/tests/config_datasource_test.cpp`

**Interfaces:**
- Consumes: the one-argument constructors from Task 4.
- Produces: `tardis.trades=` and `tardis.quotes=` keys handled in `SourceCollector::walk`. `add_tardis()` no longer exists.

- [ ] **Step 1: Write the failing test**

Add to `src/tests/config_datasource_test.cpp`. Both synthetic files carry the **same** `exchange` and `symbol`, which is what makes them one instrument, and they interleave in time so a broken merge cannot pass.

```cpp
#include <chrono>
#include <variant>
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/stream/quote.hpp"

///one INI, two data types, one instrument
static void test_tardis_keys() {
    auto dir = std::filesystem::temp_directory_path() / "qb_tardis_cfg_test";
    std::filesystem::create_directories(dir);

    write_gz((dir/"t.csv.gz").string(),
        "exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n"
        "bitmex,XBTUSD,1585699200000000,1585699201000000,a,buy,100,1\n"
        "bitmex,XBTUSD,1585699202000000,1585699203000000,b,sell,102,1\n");
    write_gz((dir/"q.csv.gz").string(),
        "exchange,symbol,timestamp,local_timestamp,ask_amount,ask_price,bid_price,bid_amount\n"
        "bitmex,XBTUSD,1585699201000000,1585699202000000,5,101,100.5,5\n"
        "bitmex,XBTUSD,1585699203000000,1585699204000000,5,103,102.5,5\n");
    {
        std::ofstream ini(dir/"both.ini");
        ini << "[data-source]\n"
               "tardis.quotes=q.csv.gz\n"
               "tardis.trades=t.csv.gz\n";
    }

    auto ds = configure_datasources(dir/"both.ini");
    std::string order;
    BacktestEvent ev;
    std::chrono::system_clock::time_point prev = {};
    while (ds(ev)) {
        CHECK_EQUAL(ev.symbol, std::string("bitmex:XBTUSD"));
        CHECK(ev.time >= prev);
        prev = ev.time;
        order.push_back(std::holds_alternative<quarkbot::Trade>(ev.data) ? 'T' : 'Q');
    }
    // trades and quotes interleave, which only happens if both landed in one heap
    CHECK_EQUAL(order, std::string("TQTQ"));

    // an unknown data type names the key and the config file
    {
        std::ofstream ini(dir/"badtype.ini");
        ini << "[data-source]\n"
               "tardis.orderbook=q.csv.gz\n";
    }
    CHECK_EXCEPTION_EXPR(std::runtime_error, e,
        std::string_view(e.what()).find("tardis.orderbook") != std::string_view::npos
        && std::string_view(e.what()).find("badtype.ini") != std::string_view::npos,
        configure_datasources(dir/"badtype.ini"));

    // the retired bare key says what to use instead
    {
        std::ofstream ini(dir/"legacy.ini");
        ini << "[data-source]\n"
               "tardis=t.csv.gz\n";
    }
    CHECK_EXCEPTION_EXPR(std::runtime_error, e,
        std::string_view(e.what()).find("tardis.trades") != std::string_view::npos,
        configure_datasources(dir/"legacy.ini"));

    std::filesystem::remove_all(dir);
}
```

Add `test_tardis_keys();` to `main()`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_config_datasource_test && ./build/tests/test_config_datasource_test`
Expected: FAIL — `tardis.quotes` hits the final `else` and throws `Unknown key tardis.quotes`, so the first `configure_datasources` call throws before any assertion.

- [ ] **Step 3: Write minimal implementation**

In `src/quarkbot/backtest/config_datasource.cpp`, replace the `else if (row.key == "tardis") add_tardis(root/row.value);` line with:

```cpp
                else if (row.key.starts_with("tardis.")) {
                    auto t = row.key.substr(7);
                    if (t == "trades") sources.push_back(TardisTradesDataSource(root/row.value));
                    else if (t == "quotes") sources.push_back(TardisQuotesDataSource(root/row.value));
                    else throw std::runtime_error(std::format(
                        "Unknown tardis data type: `{}`, Expected: trades, quotes in config `{}`",
                        row.key, fcan.string()));
                }
                else if (row.key == "tardis") throw std::runtime_error(std::format(
                    "Key `tardis` is no longer supported, use `tardis.trades` or "
                    "`tardis.quotes` in config `{}`", fcan.string()));
```

Delete the `add_tardis` member function. `"tardis."` is 7 characters. No `#ifdef` is needed: `src/quarkbot/CMakeLists.txt:19` adds the `tardis` subdirectory unconditionally.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build`
Expected: all pass, including `Passed: order==std::string("TQTQ")`.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/backtest/config_datasource.cpp src/tests/config_datasource_test.cpp
git commit -m "feat(tardis): wire the source into the data-source configuration"
```

---

### Task 9: Documentation

**Files:**
- Modify: `src/quarkbot/backtest/config_datasource.hpp` (the `configure_datasources` doc comment)
- Modify: `CLAUDE.md` (the `tardis/` component line under "Implementation Layer")
- Modify: `src/quarkbot/algoseek/algoseek_data_source.hpp:126-127` (comment describing `Decimal`'s bare throw)

**Interfaces:**
- Consumes: the key names from Task 8.
- Produces: nothing consumed by other tasks.

- [ ] **Step 1: Document the keys**

In `src/quarkbot/backtest/config_datasource.hpp`, replace the `tardis=file.gz` line of the key legend with:

```
tardis.trades=file.csv.gz  ;tardis.dev trades export - gzip CSV
tardis.quotes=file.csv.gz  ;tardis.dev quotes or book_ticker export - gzip CSV
```

and add this paragraph after the algoseek section:

```
The tardis.trades and tardis.quotes keys read tardis.dev CSV exports. Both may
repeat and both are independent of order. There are no tardis.* option keys: the
symbol is taken from the file's own exchange and symbol columns as
`exchange:symbol` (rename it in [symbol-mapping] if a strategy needs another
name), the timestamps are microseconds by the vendor's definition, and the event
time is always the local_timestamp column - the instant the data arrived, which
is the earliest a strategy could have acted on it.

tardis.quotes also accepts a book_ticker export; the two data types share one
schema. Trades and quotes of one instrument need no pairing in the configuration
- both report the same symbol, so they merge into one instrument on their own:

[data-source]
tardis.trades=bitmex_trades_2020-04-01_XBTUSD.csv.gz
tardis.quotes=bitmex_quotes_2020-04-01_XBTUSD.csv.gz
```

- [ ] **Step 2: Correct `CLAUDE.md`**

`CLAUDE.md` names two build options that **do not exist**. `CMakeLists.txt:24-26` declares only `QUARKBOT_NETWORK`, `QUARKBOT_LEVELDB` and `QUARKBOT_TESTS`; there is no `QUARKBOT_TARDIS` and no `QUARKBOT_TRTH`. `src/quarkbot/CMakeLists.txt:19-21` adds `tardis`, `trth` and `algoseek` unconditionally, while `network` (`:15`) and `leveldb` (`:18`) really are guarded.

Make three edits.

Line 98 — replace:

```
- **`tardis/`** (opt: `QUARKBOT_TARDIS`) — `TardisDataSource` historical importer.
```

with:

```
- **`tardis/`** — `TardisTradesDataSource` / `TardisQuotesDataSource`, tardis.dev CSV export importers, wired to the `tardis.trades` / `tardis.quotes` config keys.
```

Line 99 — drop the nonexistent option and add the missing `algoseek/` entry after it:

```
- **`trth/`** — Refinitiv TRTH event/raw source importers.
- **`algoseek/`** — `AlgoseekDataSource`, Algoseek US equity "Trades Only" importer, wired to the `algoseek` config key.
```

Line 35 — the sentence claims `QUARKBOT_TARDIS` and `QUARKBOT_TRTH` are options that are OFF by default. Replace it with:

```
`QUARKBOT_TESTS` is ON by default when the project is built top-level, and it force-enables the network component so the full test suite can compile. `QUARKBOT_NETWORK` and `QUARKBOT_LEVELDB` are OFF by default because they pull in external deps (openssl, leveldb). The tardis, trth and algoseek components are always built; zlib is required unconditionally at the top level.
```

Also correct the build-command block near the top of `CLAUDE.md`, which passes `-DQUARKBOT_TARDIS=ON -DQUARKBOT_TRTH=ON`; those two flags do nothing.

- [ ] **Step 3: Correct the stale algoseek comment**

`src/quarkbot/algoseek/algoseek_data_source.hpp:126-127` describes `Decimal::from_string`'s "bare `throw \"...\"`", which Task 1 removed. Replace the comment with:

```cpp
    ///parse a decimal column, turning a Decimal::from_string failure into a
    ///row_error naming the file, row and column
```

- [ ] **Step 4: Verify**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build`
Expected: all pass — these are comments and Markdown, so nothing should change, but confirm the doc comment edit did not break the header.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/backtest/config_datasource.hpp CLAUDE.md src/quarkbot/algoseek/algoseek_data_source.hpp
git commit -m "docs: document the tardis config keys and correct stale component notes"
```

---

## Verification

After Task 9:

- [ ] `ctest --test-dir build` — all tests pass
- [ ] A fresh configure works: `rm -rf /tmp/qbcheck && cmake -DCMAKE_CXX_COMPILER=g++-14 -S . -B /tmp/qbcheck` succeeds (catches a fixture listed in CMake but absent, the failure mode that `e1f24a9` shipped)
- [ ] `git status --short` shows no `data/` entry — the 113 MB exports stay out of the history
- [ ] `du -sh src/tests/data` is under 200 KB
- [ ] `grep -rn 'catch (\.\.\.)' src/quarkbot/tardis/` returns nothing
- [ ] `grep -rn 'bidPrice\|askSize\|localTimestamp\|parse_ns_timestamp' src/` returns nothing
