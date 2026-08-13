# Algoseek Data Source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a backtest data source that replays Algoseek US equity "Trades Only" gzip CSV exports as `Trade` and final `Auction` events, configured from the INI `[data-source]` section.

**Architecture:** A new component directory `src/quarkbot/algoseek/` contributing three units to `quarkbot_impl`: a pure spec-string parser, a DST-aware local-to-UTC converter with a cached offset, and the data source itself which reads the gzip CSV through the existing `CSVReader`. The `algoseek=` key in `config_datasource.cpp` wires it into the configuration.

**Tech Stack:** C++23, zlib, `std::chrono` tzdb (`std::chrono::locate_zone`), the in-tree `CSVReader` (`include/quarkbot/utils/csv_reader.h`), `Decimal` (`include/quarkbot/decimal.hpp`), the `check.h` macro test harness.

**Spec:** `docs/superpowers/specs/2026-08-12-algoseek-data-source-design.md`

## Global Constraints

- Build with `g++-14`: `cmake -DCMAKE_CXX_COMPILER=g++-14 ..`. The default compiler cannot build this repo.
- Financial values use `Decimal` from `quarkbot/decimal.hpp`, never `double`.
- The system tzdata files must be present at runtime; `std::chrono::locate_zone` reads them. No extra link flag is needed with libstdc++ 14.
- Header include style inside the impl tree is `#include "quarkbot/<component>/<file>.hpp"` (the impl include root is `src/`).
- `CHECK_EXCEPTION` takes the exception type **first**: `CHECK_EXCEPTION(std::runtime_error, expr)`. The `CLAUDE.md` description has the argument order backwards.
- `IniReader` only treats a line as a comment when the line *starts* with `;` or `#`. A trailing comment after a value becomes part of the value, so documentation examples must not use inline comments.
- Every test binary is built from a single `.cpp` listed in `BASIC_TESTS` in `src/tests/CMakeLists.txt` and links `quarkbot::sdk quarkbot::backtest`.
- Run one test with `./build/tests/test_algoseek_source_test`, or the suite with `ctest --test-dir build`.

## File Structure

| File | Responsibility |
|---|---|
| `src/quarkbot/algoseek/CMakeLists.txt` | Component build: adds sources to `quarkbot_impl`, links zlib |
| `src/quarkbot/algoseek/algoseek_spec.hpp` / `.cpp` | `AlgoseekSpec` + `parse_algoseek_spec` — pure string parsing, no I/O |
| `src/quarkbot/algoseek/local_time_converter.hpp` | `LocalTimeConverter` — local wall clock to UTC, caching the offset interval |
| `src/quarkbot/algoseek/algoseek_data_source.hpp` / `.cpp` | `AlgoseekDataSource` — gzip CSV reading, filtering, row-to-event mapping |
| `src/quarkbot/CMakeLists.txt` | Modified: `add_subdirectory("algoseek")` |
| `src/quarkbot/backtest/config_datasource.cpp` | Modified: `algoseek=` key dispatch |
| `src/quarkbot/backtest/config_datasource.hpp` | Modified: doc comment lists the new key |
| `src/tests/algoseek_source_test.cpp` | All test groups A–E |
| `src/tests/CMakeLists.txt` | Modified: register the test, pass `TEST_DATA_PATH` |
| `src/tests/data/20240418_NASDAQ_DHIL.csv.gz` | Real fixture, 7 KB |
| `src/tests/data/20230609_BIPC.csv.gz` | Real fixture, 59 KB |

---

### Task 1: Component skeleton and spec parser

**Files:**
- Create: `src/quarkbot/algoseek/algoseek_spec.hpp`
- Create: `src/quarkbot/algoseek/algoseek_spec.cpp`
- Create: `src/quarkbot/algoseek/CMakeLists.txt`
- Create: `src/tests/algoseek_source_test.cpp`
- Modify: `src/quarkbot/CMakeLists.txt` (after the `add_subdirectory("trth" ...)` line)
- Modify: `src/tests/CMakeLists.txt` (the `BASIC_TESTS` list, and the `target_compile_definitions` block inside the `foreach`)

**Interfaces:**
- Consumes: `quarkbot::trim`, `quarkbot::split` from `quarkbot/utils/string_utils.hpp`.
- Produces:
  - `struct quarkbot::AlgoseekSpec { std::filesystem::path file; std::string exchange; const std::chrono::time_zone *tz; std::string symbol; }`
  - `quarkbot::AlgoseekSpec quarkbot::parse_algoseek_spec(std::string_view spec)`
  - `src/tests/algoseek_source_test.cpp` with `int main()` and a `TEST_DATA_PATH` macro available to it.

- [ ] **Step 1: Write the failing test**

Create `src/tests/algoseek_source_test.cpp`:

```cpp
#include "quarkbot/algoseek/algoseek_spec.hpp"
#include "check.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace quarkbot;

static void test_spec_parsing() {
    std::cout << "--- test_spec_parsing" << std::endl;

    // bare path: no exchange filter, UTC, no symbol override
    {
        auto s = parse_algoseek_spec("IBM.csv.gz");
        CHECK_EQUAL(s.file.string(), std::string("IBM.csv.gz"));
        CHECK(s.exchange.empty());
        CHECK(s.symbol.empty());
        CHECK(s.tz != nullptr);
        // compare by identity, not by name: "UTC" is a tzdata link whose
        // canonical name is "Etc/UTC", so asserting on name() would fail
        CHECK(s.tz == std::chrono::locate_zone("UTC"));
    }

    // all parameters
    {
        auto s = parse_algoseek_spec(
            "data/IBM.csv.gz?exchange=NASDAQ&tzone=America/New_York&symbol=IBM.NASDAQ");
        CHECK_EQUAL(s.file.string(), std::string("data/IBM.csv.gz"));
        CHECK_EQUAL(s.exchange, std::string("NASDAQ"));
        CHECK_EQUAL(s.symbol, std::string("IBM.NASDAQ"));
        CHECK_EQUAL(std::string(s.tz->name()), std::string("America/New_York"));
    }

    // parameter order does not matter, exchange values may contain a space
    {
        auto s = parse_algoseek_spec("IBM.csv.gz?tzone=America/New_York&exchange=BATS Y");
        CHECK_EQUAL(s.exchange, std::string("BATS Y"));
        CHECK_EQUAL(std::string(s.tz->name()), std::string("America/New_York"));
    }

    // empty query after '?' behaves like a bare path
    {
        auto s = parse_algoseek_spec("IBM.csv.gz?");
        CHECK(s.exchange.empty());
        CHECK(s.tz == std::chrono::locate_zone("UTC"));
    }

    // trailing '&' is tolerated
    {
        auto s = parse_algoseek_spec("IBM.csv.gz?exchange=NYSE&");
        CHECK_EQUAL(s.exchange, std::string("NYSE"));
    }

    CHECK_EXCEPTION(std::runtime_error, parse_algoseek_spec("IBM.csv.gz?bogus=1"));
    CHECK_EXCEPTION(std::runtime_error, parse_algoseek_spec("IBM.csv.gz?exchange"));
    CHECK_EXCEPTION(std::runtime_error, parse_algoseek_spec("IBM.csv.gz?tzone=Mars/Olympus"));
    CHECK_EXCEPTION(std::runtime_error, parse_algoseek_spec("?exchange=NYSE"));

    // the error message names the offending key
    CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("bogus") != std::string_view::npos,
            parse_algoseek_spec("IBM.csv.gz?bogus=1"));
}

int main() {
    test_spec_parsing();
    std::cout << "All tests passed" << std::endl;
    return 0;
}
```

- [ ] **Step 2: Register the component and the test so the build reaches the new code**

In `src/quarkbot/CMakeLists.txt`, add after the `add_subdirectory("trth" EXCLUDE_FROM_ALL)` line:

```cmake
add_subdirectory("algoseek" EXCLUDE_FROM_ALL)
```

Create `src/quarkbot/algoseek/CMakeLists.txt`:

```cmake
find_package(ZLIB REQUIRED)

target_sources(quarkbot_impl PRIVATE
        algoseek_spec.cpp
        algoseek_spec.hpp)
target_link_libraries(quarkbot_impl PUBLIC ZLIB::ZLIB)
```

In `src/tests/CMakeLists.txt`, add to the `BASIC_TESTS` list (after `tardis_source_test.cpp`):

```cmake
    algoseek_source_test.cpp
```

and extend the `target_compile_definitions` call inside the existing `foreach` loop so the fixture directory is available to every test binary:

```cmake
    target_compile_definitions(${executable_name} PRIVATE
        MODULE_PATH="${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        TEST_DATA_PATH="${CMAKE_CURRENT_LIST_DIR}/data"
    )
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-14 && cmake --build build --target test_algoseek_source_test -j$(nproc)
```

Expected: FAIL at compile time with `quarkbot/algoseek/algoseek_spec.hpp: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `src/quarkbot/algoseek/algoseek_spec.hpp`:

```cpp
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace quarkbot {

///Parsed specification of an Algoseek data source
struct AlgoseekSpec {
    ///path to the gzipped CSV file; relative paths must be resolved by the caller
    std::filesystem::path file;
    ///exchange filter matched verbatim against the Exchange column, empty means no filtering
    std::string exchange;
    ///time zone of the file's wall clock timestamps
    const std::chrono::time_zone *tz = nullptr;
    ///symbol reported on emitted events, empty means use the Ticker column
    std::string symbol;
};

///Parse a data source specification
/**
    Syntax: <file>[?exchange=<name>&tzone=<iana zone>&symbol=<symbol>]

    All parameters are optional. A missing exchange means no filtering, a missing
    tzone means UTC, a missing symbol means the Ticker column is used.

    @param spec specification string, as written in the INI configuration
    @return parsed specification with a relative file path
    @exception std::runtime_error empty path, malformed parameter, unknown
        parameter key, or unknown time zone name
*/
AlgoseekSpec parse_algoseek_spec(std::string_view spec);

}
```

- [ ] **Step 5: Write the implementation**

Create `src/quarkbot/algoseek/algoseek_spec.cpp`:

```cpp
#include "algoseek_spec.hpp"

#include "quarkbot/utils/string_utils.hpp"
#include <format>
#include <stdexcept>

namespace quarkbot {

AlgoseekSpec parse_algoseek_spec(std::string_view spec) {
    AlgoseekSpec res;
    auto qpos = spec.find('?');
    auto path = trim(spec.substr(0, qpos));
    if (path.empty()) {
        throw std::runtime_error(std::format("Algoseek source: empty file path in '{}'", spec));
    }
    res.file = std::filesystem::path(path);

    std::string_view tzone;
    if (qpos != spec.npos) {
        std::string_view query = spec.substr(qpos + 1);
        while (!query.empty()) {
            std::string_view param = split(query, "&");
            if (param.empty()) continue;
            auto eq = param.find('=');
            if (eq == param.npos) {
                throw std::runtime_error(std::format(
                    "Algoseek source: parameter '{}' has no value in '{}'", param, spec));
            }
            auto key = trim(param.substr(0, eq));
            auto value = trim(param.substr(eq + 1));
            if (key == "exchange") res.exchange = value;
            else if (key == "tzone") tzone = value;
            else if (key == "symbol") res.symbol = value;
            else throw std::runtime_error(std::format(
                "Algoseek source: unknown parameter '{}' in '{}'", key, spec));
        }
    }

    try {
        res.tz = std::chrono::locate_zone(tzone.empty() ? std::string_view("UTC") : tzone);
    } catch (const std::exception &e) {
        throw std::runtime_error(std::format(
            "Algoseek source: unknown time zone '{}' in '{}': {}", tzone, spec, e.what()));
    }
    return res;
}

}
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc) && ./build/tests/test_algoseek_source_test
```

Expected: PASS, ending with `All tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/quarkbot/algoseek/ src/quarkbot/CMakeLists.txt src/tests/algoseek_source_test.cpp src/tests/CMakeLists.txt
git commit -m "feat(algoseek): parse data source specification strings"
```

---

### Task 2: Local time to UTC converter

**Files:**
- Create: `src/quarkbot/algoseek/local_time_converter.hpp`
- Modify: `src/quarkbot/algoseek/CMakeLists.txt` (add the header to `target_sources`)
- Modify: `src/tests/algoseek_source_test.cpp` (add `test_local_time_converter`, call it from `main`)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `class quarkbot::LocalTimeConverter`, constructed as `LocalTimeConverter(const std::chrono::time_zone *tz)`, with the single method
  `std::chrono::system_clock::time_point to_sys(std::chrono::local_time<std::chrono::nanoseconds> lt)`.

- [ ] **Step 1: Write the failing test**

Add to `src/tests/algoseek_source_test.cpp` (above `main`):

```cpp
#include "quarkbot/algoseek/local_time_converter.hpp"

///build a local_time from Y/M/D and a time of day, for readable test data
static std::chrono::local_time<std::chrono::nanoseconds> mk_local(
        int y, unsigned m, unsigned d, int hh, int mm, int ss, long long ns) {
    using namespace std::chrono;
    return local_time<nanoseconds>{
        local_days{year{y}/month{m}/day{d}}.time_since_epoch()
        + hours{hh} + minutes{mm} + seconds{ss} + nanoseconds{ns}};
}

///parse an ISO instant like "2026-04-09 08:00:00.010833553" into a time_point
static std::chrono::system_clock::time_point mk_utc(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::chrono::sys_time<std::chrono::nanoseconds> tp;
    in >> std::chrono::parse("%F %T", tp);
    if (in.fail()) { std::cerr << "bad test instant: " << text << std::endl; exit(1); }
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(tp);
}

static void test_local_time_converter() {
    std::cout << "--- test_local_time_converter" << std::endl;

    LocalTimeConverter et(std::chrono::locate_zone("America/New_York"));

    // EDT (-04:00), nanoseconds preserved
    CHECK(et.to_sys(mk_local(2026, 4, 9, 4, 0, 0, 10833553))
            == mk_utc("2026-04-09 08:00:00.010833553"));

    // the two auction instants asserted later against the real BIPC fixture
    CHECK(et.to_sys(mk_local(2023, 6, 9, 9, 30, 0, 791480832))
            == mk_utc("2023-06-09 13:30:00.791480832"));
    CHECK(et.to_sys(mk_local(2023, 6, 9, 16, 0, 2, 164273920))
            == mk_utc("2023-06-09 20:00:02.164273920"));

    // EST (-05:00), then EDT, then EST again on the same instance:
    // exercises cache invalidation in both directions
    CHECK(et.to_sys(mk_local(2024, 1, 15, 9, 30, 0, 0)) == mk_utc("2024-01-15 14:30:00"));
    CHECK(et.to_sys(mk_local(2024, 7, 15, 9, 30, 0, 0)) == mk_utc("2024-07-15 13:30:00"));
    CHECK(et.to_sys(mk_local(2024, 1, 15, 9, 30, 0, 0)) == mk_utc("2024-01-15 14:30:00"));

    // UTC is the identity
    LocalTimeConverter utc(std::chrono::locate_zone("UTC"));
    CHECK(utc.to_sys(mk_local(2026, 4, 9, 4, 0, 0, 10833553))
            == mk_utc("2026-04-09 04:00:00.010833553"));

    // the two DST rules the class documents, each on a cold cache
    {
        LocalTimeConverter amb(std::chrono::locate_zone("America/New_York"));
        CHECK(amb.to_sys(mk_local(2023, 11, 5, 1, 30, 0, 0))
                == mk_utc("2023-11-05 05:30:00"));
    }
    {
        LocalTimeConverter gap(std::chrono::locate_zone("America/New_York"));
        CHECK(gap.to_sys(mk_local(2023, 3, 12, 2, 30, 0, 0))
                == mk_utc("2023-03-12 07:30:00"));
    }
    // a pre-epoch local time must still take the lookup path
    {
        LocalTimeConverter old(std::chrono::locate_zone("America/New_York"));
        CHECK(old.to_sys(mk_local(1969, 12, 31, 23, 59, 59, 500000000))
                == mk_utc("1970-01-01 04:59:59.500000000"));
    }
}
```

Add `#include <sstream>` to the includes at the top of the file, and call the new
function from `main` before the final message:

```cpp
int main() {
    test_spec_parsing();
    test_local_time_converter();
    std::cout << "All tests passed" << std::endl;
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc)
```

Expected: FAIL at compile time with `quarkbot/algoseek/local_time_converter.hpp: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `src/quarkbot/algoseek/local_time_converter.hpp`:

```cpp
#pragma once

#include <chrono>

namespace quarkbot {

///Converts local wall clock timestamps to UTC, caching the current UTC offset
/**
    Data files carry local wall clock time with no offset, so every timestamp
    needs a time zone lookup. Looking up each row would be wasteful: a lookup
    result is valid for a whole offset interval (typically half a year), which
    the class caches together with the interval bounds. A single-day file
    therefore costs one lookup rather than one per row.

    Ambiguous local times (the repeated hour of an autumn DST transition)
    resolve to the earlier of the two instants. Local times that do not exist
    (the skipped hour of a spring transition) resolve using the offset in
    effect before the transition; such timestamps cannot occur in exchange
    trading data.
*/
class LocalTimeConverter {
public:

    ///construct for the given time zone
    /**
        @param tz time zone, must outlive the converter; zones returned by
            std::chrono::locate_zone are owned by the tzdb and always do
    */
    explicit LocalTimeConverter(const std::chrono::time_zone *tz):_tz(tz) {}

    ///convert a local timestamp to UTC
    std::chrono::system_clock::time_point to_sys(
            std::chrono::local_time<std::chrono::nanoseconds> lt) {
        auto ns = lt.time_since_epoch() - _offset;
        //compare in seconds: the sys_info bounds of the first and last offset
        //interval are sys_seconds min/max, which cannot be promoted to
        //nanoseconds without signed overflow
        auto secs = std::chrono::floor<std::chrono::seconds>(
                std::chrono::sys_time<std::chrono::nanoseconds>(ns));
        if (secs < _begin || secs >= _end) {
            auto info = _tz->get_info(lt);
            _offset = info.first.offset;
            _begin = info.first.begin;
            _end = info.first.end;
            ns = lt.time_since_epoch() - _offset;
        }
        return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                std::chrono::sys_time<std::chrono::nanoseconds>(ns));
    }

protected:
    const std::chrono::time_zone *_tz;
    std::chrono::seconds _offset = {};
    //an empty interval, so the first conversion always performs a lookup.
    //The seconds-domain comparison in to_sys() is what makes this safe for
    //every input, including pre-epoch ones.
    std::chrono::sys_seconds _begin{std::chrono::seconds::max()};
    std::chrono::sys_seconds _end{std::chrono::seconds::min()};
};

}
```

Add the header to `target_sources` in `src/quarkbot/algoseek/CMakeLists.txt`:

```cmake
target_sources(quarkbot_impl PRIVATE
        algoseek_spec.cpp
        algoseek_spec.hpp
        local_time_converter.hpp)
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc) && ./build/tests/test_algoseek_source_test
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/algoseek/ src/tests/algoseek_source_test.cpp
git commit -m "feat(algoseek): local wall clock to UTC converter with cached offset"
```

---

### Task 3: Data source reading trades

Reads the gzip CSV and emits `Trade` events. Auctions and administrative skips arrive in Task 4, so the test data here uses only ordinary `TRADE` and `TRADE NB` rows.

**Files:**
- Create: `src/quarkbot/algoseek/algoseek_data_source.hpp`
- Create: `src/quarkbot/algoseek/algoseek_data_source.cpp`
- Modify: `src/quarkbot/algoseek/CMakeLists.txt`
- Modify: `src/tests/algoseek_source_test.cpp`

**Interfaces:**
- Consumes: `quarkbot::AlgoseekSpec` (Task 1), `quarkbot::LocalTimeConverter` (Task 2).
- Produces:
  - `enum class quarkbot::AlgoseekTradeFlag: unsigned` with enumerators `opening_prints = 6`, `closing_prints = 7`, `reopening_prints = 8`, `official_close = 24`, `official_open = 26`
  - `constexpr bool quarkbot::has_flag(std::uint32_t flags, AlgoseekTradeFlag f)`
  - `class quarkbot::AlgoseekDataSource` with `explicit AlgoseekDataSource(AlgoseekSpec spec)` and `bool operator()(BacktestEvent &ev)`. It is move-constructible and not copyable, so it satisfies `BacktestDataSource`.
  - Test helper `static void write_gz(const std::string &path, std::string_view content)`.

- [ ] **Step 1: Write the failing test**

Add to `src/tests/algoseek_source_test.cpp`:

```cpp
#include "quarkbot/algoseek/algoseek_data_source.hpp"
#include <zlib.h>
#include <variant>

static void write_gz(const std::string &path, std::string_view content) {
    gzFile f = gzopen(path.c_str(), "wb");
    if (!f) { std::cerr << "Cannot open gz for write: " << path << std::endl; exit(1); }
    gzwrite(f, content.data(), static_cast<unsigned>(content.size()));
    gzclose(f);
}

static const std::string_view ALGOSEEK_HEADER =
    "Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange,Conditions\n";

static void test_trades() {
    std::cout << "--- test_trades" << std::endl;

    const std::string path = "/tmp/test_algoseek_trades.csv.gz";
    write_gz(path, std::string(ALGOSEEK_HEADER) +
        "20230609,04:00:00.010833553,TRADE,IBM,242.54,6,EDGX,80002000\n"
        "20230609,09:30:01.500000000,TRADE NB,IBM,243.10,250,NASDAQ,20002020\n");

    auto spec = parse_algoseek_spec(path + "?tzone=America/New_York");
    AlgoseekDataSource src(std::move(spec));

    BacktestEvent e1;
    CHECK(src(e1));
    CHECK_EQUAL(e1.symbol, std::string("IBM"));
    CHECK(e1.time == mk_utc("2023-06-09 08:00:00.010833553"));
    CHECK(std::holds_alternative<Trade>(e1.data));
    {
        auto &t = std::get<Trade>(e1.data);
        CHECK(t.price == Decimal::from_string("242.54"));
        CHECK(t.size == Decimal::from_string("6"));
        CHECK(t.side == Side::undetermined);
        CHECK(t.time == e1.time);
    }

    // TRADE NB is a real trade too, not a duplicate of a TRADE row
    BacktestEvent e2;
    CHECK(src(e2));
    CHECK(std::holds_alternative<Trade>(e2.data));
    CHECK(std::get<Trade>(e2.data).price == Decimal::from_string("243.10"));
    CHECK(e2.time == mk_utc("2023-06-09 13:30:01.500000000"));

    BacktestEvent e3;
    CHECK(!src(e3));
    // exhausted source keeps returning false
    CHECK(!src(e3));

    // usable as a BacktestDataSource
    {
        BacktestDataSource ds = AlgoseekDataSource(
                parse_algoseek_spec(path + "?tzone=America/New_York"));
        BacktestEvent ev;
        CHECK(ds(ev));
        CHECK(std::holds_alternative<Trade>(ev.data));
    }
}
```

Add these includes at the top of the test file: `#include "quarkbot/decimal.hpp"`,
`#include "quarkbot/stream/trade.hpp"`, `#include "quarkbot/abstract/backtest_data_source.hpp"`.
Call `test_trades();` from `main` after `test_local_time_converter();`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc)
```

Expected: FAIL at compile time with `quarkbot/algoseek/algoseek_data_source.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/quarkbot/algoseek/algoseek_data_source.hpp`:

```cpp
#pragma once

#include "algoseek_spec.hpp"
#include "local_time_converter.hpp"

#include "quarkbot/abstract/backtest_data_source.hpp"
#include <quarkbot/utils/csv_reader.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace quarkbot {

///Bit positions in the Algoseek Conditions field used by this source
/**
    The field is a 32 bit mask written as 8 hexadecimal digits. Only the bits
    this source acts upon are named here; the full list is documented in
    algoseek.US.Equity.Trades.Only.pdf
*/
enum class AlgoseekTradeFlag: unsigned {
    ///opening auction print
    opening_prints = 6,
    ///closing auction print
    closing_prints = 7,
    ///reopening auction print, follows a trading halt
    reopening_prints = 8,
    ///official closing price re-broadcast, not a trade
    official_close = 24,
    ///official opening price re-broadcast, not a trade
    official_open = 26,
};

///test a single flag in a Conditions bitmask
constexpr bool has_flag(std::uint32_t flags, AlgoseekTradeFlag f) {
    return ((flags >> static_cast<unsigned>(f)) & 1U) != 0;
}

///Backtest data source generating Trade and Auction events from an Algoseek trades export
/**
    Reads one gzipped CSV file with the eight column layout
    Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange,Conditions

    Only Trade and final Auction events are produced; the format carries no
    quotes and no indicative auction data. Rows that do not represent a
    tradable execution are counted and skipped, and the counters are logged
    when the file is exhausted.
*/
class AlgoseekDataSource {
public:

    ///open the source described by the specification
    /**
        @param spec parsed specification with a resolved file path
        @exception std::runtime_error file cannot be opened, or a required
            column is missing from the header
    */
    explicit AlgoseekDataSource(AlgoseekSpec spec);

    ///Retrieve next event
    /**
        @param ev reference to variable filled with event
        @retval true success
        @retval false eof reached
        @exception std::runtime_error malformed row
    */
    bool operator()(BacktestEvent &ev);

protected:

    struct CSVSource {
        std::move_only_function<std::string_view()> block_reader;
        std::string_view cur_line = {};
        int operator()();
    };

    struct Data {
        std::string date;
        std::string timestamp;
        std::string event_type;
        std::string ticker;
        std::string price;
        std::string quantity;
        std::string exchange;
        std::string conditions;
    };

    struct Counters {
        std::uint64_t trades = 0;
        std::uint64_t auctions = 0;
        std::uint64_t cancelled = 0;
        std::uint64_t unknown_event = 0;
        std::uint64_t filtered_exchange = 0;
        std::uint64_t zero_qty = 0;
        std::uint64_t zero_price = 0;
        std::uint64_t official_print = 0;
    };

    AlgoseekSpec _spec;
    LocalTimeConverter _tz;
    CSVReader<CSVSource> _csv;
    CSVFieldIndexMapping<Data> _colmap;
    Data _row = {};
    Counters _counters = {};
    ///number of the row last read; the header is row 1
    std::uint64_t _line = 1;
    bool _eof = false;

    static CSVSource init_source(const std::filesystem::path &file);
    static CSVFieldIndexMapping<Data> map_columns(CSVReader<CSVSource> &csv,
            const std::filesystem::path &file);

    ///throw a runtime_error naming the file and the current row
    [[noreturn]] void row_error(std::string_view message) const;
    ///parse the Conditions column, throws on anything but 8 hex digits
    std::uint32_t parse_conditions() const;
    ///combine the Date and Timestamp columns into a local timestamp
    std::chrono::local_time<std::chrono::nanoseconds> parse_local_time() const;
    ///log the skip counters
    void log_summary() const;
};

}
```

- [ ] **Step 4: Write the implementation**

Create `src/quarkbot/algoseek/algoseek_data_source.cpp`:

```cpp
#include "algoseek_data_source.hpp"

#include "quarkbot/log.hpp"
#include <array>
#include <charconv>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
#include <zlib.h>

namespace quarkbot {

int AlgoseekDataSource::CSVSource::operator()() {
    if (cur_line.empty()) cur_line = block_reader();
    if (cur_line.empty()) return -1;
    unsigned char c = static_cast<unsigned char>(cur_line.front());
    cur_line.remove_prefix(1);
    return static_cast<int>(c);
}

AlgoseekDataSource::CSVSource AlgoseekDataSource::init_source(
        const std::filesystem::path &file) {
    #if defined(_WIN32)
        auto gzf = gzopen_w(file.c_str(), "r");
    #else
        auto gzf = gzopen(file.c_str(), "r");
    #endif
    if (gzf == nullptr) {
        throw std::runtime_error(std::format(
            "Algoseek source: failed to open gz file: {}", file.string()));
    }
    auto shared_gzf = std::shared_ptr<struct gzFile_s>(gzf, [](gzFile f){gzclose(f);});
    return CSVSource{
        [shared_gzf, buff = std::array<char, 65536>()]() mutable -> std::string_view {
            int r = gzread(shared_gzf.get(), buff.data(), static_cast<unsigned int>(buff.size()));
            if (r > 0) return {buff.data(), static_cast<std::size_t>(r)};
            if (r == 0 && gzeof(shared_gzf.get())) return {};
            int errnum;
            const char *err = gzerror(shared_gzf.get(), &errnum);
            throw std::runtime_error(std::format("Algoseek source: gz error {}: {}", errnum, err));
        },
    };
}

CSVFieldIndexMapping<AlgoseekDataSource::Data> AlgoseekDataSource::map_columns(
        CSVReader<CSVSource> &csv, const std::filesystem::path &file) {
    auto colmap = csv.mapColumns<Data>({
        {"Date", &Data::date},
        {"Timestamp", &Data::timestamp},
        {"EventType", &Data::event_type},
        {"Ticker", &Data::ticker},
        {"Price", &Data::price},
        {"Quantity", &Data::quantity},
        {"Exchange", &Data::exchange},
        {"Conditions", &Data::conditions},
    });
    if (!colmap.allMapped) {
        std::string missing;
        auto check = [&](std::string_view name, auto ptr) {
            if (!colmap.isMapped(ptr)) {
                if (!missing.empty()) missing.append(", ");
                missing.append(name);
            }
        };
        check("Date", &Data::date);
        check("Timestamp", &Data::timestamp);
        check("EventType", &Data::event_type);
        check("Ticker", &Data::ticker);
        check("Price", &Data::price);
        check("Quantity", &Data::quantity);
        check("Exchange", &Data::exchange);
        check("Conditions", &Data::conditions);
        throw std::runtime_error(std::format(
            "Algoseek source {}: missing header column(s): {}", file.string(), missing));
    }
    return colmap;
}

AlgoseekDataSource::AlgoseekDataSource(AlgoseekSpec spec)
    :_spec(std::move(spec))
    ,_tz(_spec.tz)
    ,_csv(init_source(_spec.file))
    ,_colmap(map_columns(_csv, _spec.file))
{
}

void AlgoseekDataSource::row_error(std::string_view message) const {
    throw std::runtime_error(std::format(
        "Algoseek source {}: row {}: {}", _spec.file.string(), _line, message));
}

std::uint32_t AlgoseekDataSource::parse_conditions() const {
    std::string_view s = _row.conditions;
    std::uint32_t value = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), value, 16);
    if (s.size() != 8 || res.ec != std::errc{} || res.ptr != s.data() + s.size()) {
        row_error(std::format(
            "Conditions '{}' is not 8 hexadecimal digits; the file may have shifted "
            "columns (expected 8 fields, decimal point as '.')", s));
    }
    return value;
}

std::chrono::local_time<std::chrono::nanoseconds> AlgoseekDataSource::parse_local_time() const {
    auto number = [&](std::string_view s) {
        int v = 0;
        auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) {
            row_error(std::format("'{}' is not a number", s));
        }
        return v;
    };

    std::string_view date = _row.date;
    if (date.size() != 8) row_error(std::format("Date '{}' is not YYYYMMDD", date));
    std::chrono::year_month_day ymd{
        std::chrono::year{number(date.substr(0, 4))},
        std::chrono::month{static_cast<unsigned>(number(date.substr(4, 2)))},
        std::chrono::day{static_cast<unsigned>(number(date.substr(6, 2)))}};
    if (!ymd.ok()) row_error(std::format("Date '{}' is not a valid date", date));

    std::string_view time = _row.timestamp;
    if (time.size() != 18 || time[2] != ':' || time[5] != ':' || time[8] != '.') {
        row_error(std::format("Timestamp '{}' is not HH:MM:SS.nnnnnnnnn", time));
    }
    int hh = number(time.substr(0, 2));
    int mm = number(time.substr(3, 2));
    int ss = number(time.substr(6, 2));
    int ns = number(time.substr(9, 9));
    if (hh > 23 || mm > 59 || ss > 59) {
        row_error(std::format("Timestamp '{}' is out of range", time));
    }

    return std::chrono::local_time<std::chrono::nanoseconds>{
        std::chrono::local_days{ymd}.time_since_epoch()
        + std::chrono::hours{hh} + std::chrono::minutes{mm}
        + std::chrono::seconds{ss} + std::chrono::nanoseconds{ns}};
}

void AlgoseekDataSource::log_summary() const {
    logInfo("Algoseek source {}: {} trades, {} auctions", _spec.file.string(),
            _counters.trades, _counters.auctions);
}

bool AlgoseekDataSource::operator()(BacktestEvent &ev) {
    if (_eof) return false;

    while (true) {
        if (!_csv.readRow(_colmap, _row)) {
            _eof = true;
            log_summary();
            return false;
        }
        ++_line;

        if (_row.event_type != "TRADE" && _row.event_type != "TRADE NB") continue;

        auto quantity = Decimal::from_string(_row.quantity);
        auto price = Decimal::from_string(_row.price);
        //parsed here only to validate the row; Task 4 acts on the flags
        auto flags = parse_conditions();
        (void)flags;

        auto time = _tz.to_sys(parse_local_time());
        ev.symbol = _spec.symbol.empty() ? _row.ticker : _spec.symbol;
        ev.time = time;

        Trade &t = ev.data.emplace<Trade>();
        t.price = price;
        t.size = quantity;
        t.time = time;
        t.side = Side::undetermined;
        ++_counters.trades;
        return true;
    }
}

}
```

Add both files to `src/quarkbot/algoseek/CMakeLists.txt`:

```cmake
target_sources(quarkbot_impl PRIVATE
        algoseek_spec.cpp
        algoseek_spec.hpp
        local_time_converter.hpp
        algoseek_data_source.cpp
        algoseek_data_source.hpp)
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc) && ./build/tests/test_algoseek_source_test
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/quarkbot/algoseek/ src/tests/algoseek_source_test.cpp
git commit -m "feat(algoseek): read trades from a gzipped Algoseek CSV export"
```

---

### Task 4: Auctions, administrative skips, exchange filter, symbol override

**Files:**
- Modify: `src/quarkbot/algoseek/algoseek_data_source.cpp` (the `operator()` body and `log_summary`)
- Modify: `src/tests/algoseek_source_test.cpp`

**Interfaces:**
- Consumes: `quarkbot::AlgoseekDataSource`, `quarkbot::AlgoseekTradeFlag`, `quarkbot::has_flag` (Task 3).
- Produces: no new symbols; completes the row-to-event pipeline.

- [ ] **Step 1: Write the failing test**

Add to `src/tests/algoseek_source_test.cpp`:

```cpp
#include "quarkbot/stream/auction.hpp"

///drain a source into a vector, for tests that care about the whole sequence
static std::vector<BacktestEvent> drain(AlgoseekDataSource &src) {
    std::vector<BacktestEvent> out;
    BacktestEvent ev;
    while (src(ev)) out.push_back(std::move(ev));
    return out;
}

static void test_auctions_and_skips() {
    std::cout << "--- test_auctions_and_skips" << std::endl;

    const std::string path = "/tmp/test_algoseek_auctions.csv.gz";
    write_gz(path, std::string(ALGOSEEK_HEADER) +
        // opening print, bit 6 (0x40)
        "20230609,09:30:00.791480832,TRADE NB,BIPC,47.58,4000,NYSE,20000040\n"
        // official open re-broadcast, bit 26 (0x04000000) - dropped
        "20230609,09:30:00.791483904,TRADE,BIPC,47.58,4000,NYSE,04000000\n"
        // ordinary trade
        "20230609,10:00:00.000000000,TRADE,BIPC,47.60,100,NYSE,00000001\n"
        // reopening print, bit 8 (0x100)
        "20230609,11:00:00.000000000,TRADE,BIPC,47.20,500,NYSE,00000100\n"
        // trade that a later row cancels; it is emitted and never taken back
        "20230609,12:00:00.000000000,TRADE,BIPC,47.70,42,NYSE,00000001\n"
        // closing print, bit 7 (0x80), also carrying official_close bit 24:
        // the auction reading must win
        "20230609,16:00:02.164273920,TRADE NB,BIPC,47.92,23455,NYSE,21000080\n"
        // quantity 0 close re-broadcast, as the real files emit at 16:10 - dropped
        "20230609,16:10:00.002849536,TRADE NB,BIPC,47.92,0,NYSE,60000000\n"
        // the cancellation row itself - dropped
        "20230609,16:30:00.000000000,TRADE CANCELLED,BIPC,47.70,42,NYSE,00000001\n"
        // unexpected event type - dropped
        "20230609,16:31:00.000000000,QUOTE BID,BIPC,47.70,42,NYSE,00000001\n");

    {
        AlgoseekDataSource src(parse_algoseek_spec(path + "?tzone=America/New_York"));
        auto evs = drain(src);
        CHECK_EQUAL(evs.size(), std::size_t(5));

        // 1: opening auction
        CHECK(std::holds_alternative<Auction>(evs[0].data));
        {
            auto &a = std::get<Auction>(evs[0].data);
            CHECK(a.auction_type == AuctionType::opening);
            CHECK(a.final);
            CHECK(a.price == Decimal::from_string("47.58"));
            CHECK(a.quantity == Decimal::from_string("4000"));
            CHECK(a.quantity_traded == a.quantity);
            CHECK(a.imbalance == Decimal(0));
            CHECK(a.time == mk_utc("2023-06-09 13:30:00.791480832"));
        }

        // 2: ordinary trade (the official open row in between was dropped)
        CHECK(std::holds_alternative<Trade>(evs[1].data));
        CHECK(std::get<Trade>(evs[1].data).price == Decimal::from_string("47.60"));

        // 3: reopening auction maps to unscheduled
        CHECK(std::holds_alternative<Auction>(evs[2].data));
        CHECK(std::get<Auction>(evs[2].data).auction_type == AuctionType::unscheduled);

        // 4: the cancelled trade survives
        CHECK(std::holds_alternative<Trade>(evs[3].data));
        CHECK(std::get<Trade>(evs[3].data).size == Decimal::from_string("42"));

        // 5: closing auction wins over the official_close bit on the same row
        CHECK(std::holds_alternative<Auction>(evs[4].data));
        {
            auto &a = std::get<Auction>(evs[4].data);
            CHECK(a.auction_type == AuctionType::closing);
            CHECK(a.final);
            CHECK(a.quantity == Decimal::from_string("23455"));
            CHECK(a.time == mk_utc("2023-06-09 20:00:02.164273920"));
        }
    }
}

static void test_exchange_filter_and_symbol() {
    std::cout << "--- test_exchange_filter_and_symbol" << std::endl;

    const std::string path = "/tmp/test_algoseek_filter.csv.gz";
    write_gz(path, std::string(ALGOSEEK_HEADER) +
        "20230609,10:00:00.000000000,TRADE,IBM,47.60,100,NASDAQ,00000001\n"
        "20230609,10:00:01.000000000,TRADE,IBM,47.61,200,ARCA,00000001\n"
        "20230609,10:00:02.000000000,TRADE,IBM,47.62,300,FINRA,00000001\n"
        "20230609,10:00:03.000000000,TRADE,IBM,47.63,400,NASDAQ,00000001\n");

    // no filter: every venue passes through
    {
        AlgoseekDataSource src(parse_algoseek_spec(path));
        CHECK_EQUAL(drain(src).size(), std::size_t(4));
    }

    // filter keeps one venue only
    {
        AlgoseekDataSource src(parse_algoseek_spec(path + "?exchange=NASDAQ"));
        auto evs = drain(src);
        CHECK_EQUAL(evs.size(), std::size_t(2));
        CHECK(std::get<Trade>(evs[0].data).price == Decimal::from_string("47.60"));
        CHECK(std::get<Trade>(evs[1].data).price == Decimal::from_string("47.63"));
        CHECK_EQUAL(evs[0].symbol, std::string("IBM"));
    }

    // symbol override replaces the Ticker column
    {
        AlgoseekDataSource src(parse_algoseek_spec(path + "?exchange=ARCA&symbol=IBM.ARCA"));
        auto evs = drain(src);
        CHECK_EQUAL(evs.size(), std::size_t(1));
        CHECK_EQUAL(evs[0].symbol, std::string("IBM.ARCA"));
    }

    // an unmatched exchange yields nothing rather than an error
    {
        AlgoseekDataSource src(parse_algoseek_spec(path + "?exchange=NASDQ"));
        CHECK_EQUAL(drain(src).size(), std::size_t(0));
    }
}
```

Add `#include <vector>` to the test includes, and call both new functions from
`main` after `test_trades();`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc) && ./build/tests/test_algoseek_source_test
```

Expected: FAIL at `CHECK_EQUAL(evs.size(), std::size_t(5))` reporting 7, because
every `TRADE`/`TRADE NB` row is still emitted as a `Trade`. Seven, not nine: the
`TRADE CANCELLED` and `QUOTE BID` rows are already dropped by Task 3's
`EventType` check — which does so without counting them, until this task adds
the counters.

- [ ] **Step 3: Replace the `operator()` body**

In `src/quarkbot/algoseek/algoseek_data_source.cpp`, replace the whole
`operator()` with:

```cpp
bool AlgoseekDataSource::operator()(BacktestEvent &ev) {
    if (_eof) return false;

    while (true) {
        if (!_csv.readRow(_colmap, _row)) {
            _eof = true;
            log_summary();
            return false;
        }
        ++_line;

        //cancellations cannot be undone once replayed, so the cancelling row is
        //dropped and the cancelled trade stays; see the design spec for why a
        //look-ahead window would not work
        if (_row.event_type == "TRADE CANCELLED") {
            ++_counters.cancelled;
            continue;
        }
        if (_row.event_type != "TRADE" && _row.event_type != "TRADE NB") {
            if (_counters.unknown_event == 0) {
                logWarning("Algoseek source {}: row {}: unexpected EventType '{}', "
                        "skipping (further occurrences are only counted)",
                        _spec.file.string(), _line, _row.event_type);
            }
            ++_counters.unknown_event;
            continue;
        }
        if (!_spec.exchange.empty() && _row.exchange != _spec.exchange) {
            ++_counters.filtered_exchange;
            continue;
        }

        auto quantity = Decimal::from_string(_row.quantity);
        if (quantity <= 0) {
            ++_counters.zero_qty;
            continue;
        }
        auto price = Decimal::from_string(_row.price);
        if (price <= 0) {
            ++_counters.zero_price;
            continue;
        }

        auto flags = parse_conditions();

        AuctionType auction_type = AuctionType::unknown;
        if (has_flag(flags, AlgoseekTradeFlag::opening_prints)) {
            auction_type = AuctionType::opening;
        } else if (has_flag(flags, AlgoseekTradeFlag::closing_prints)) {
            auction_type = AuctionType::closing;
        } else if (has_flag(flags, AlgoseekTradeFlag::reopening_prints)) {
            //a reopening auction follows a halt, so it is unscheduled rather
            //than a scheduled midday auction
            auction_type = AuctionType::unscheduled;
        }

        //official open/close rows repeat the price of a real print, sometimes
        //hours later; they are not executions. Checked after the auction flags
        //so that a row carrying both is read as the auction it reports.
        if (auction_type == AuctionType::unknown
                && (has_flag(flags, AlgoseekTradeFlag::official_close)
                    || has_flag(flags, AlgoseekTradeFlag::official_open))) {
            ++_counters.official_print;
            continue;
        }

        auto time = _tz.to_sys(parse_local_time());
        ev.symbol = _spec.symbol.empty() ? _row.ticker : _spec.symbol;
        ev.time = time;

        if (auction_type != AuctionType::unknown) {
            Auction &a = ev.data.emplace<Auction>();
            a.auction_type = auction_type;
            //the export carries no indicative data, so the print is final and
            //the unavailable fields take defaults
            a.final = true;
            a.price = price;
            a.quantity = quantity;
            a.quantity_traded = quantity;
            a.imbalance = 0;
            a.time = time;
            ++_counters.auctions;
        } else {
            Trade &t = ev.data.emplace<Trade>();
            t.price = price;
            t.size = quantity;
            t.time = time;
            //the export carries no aggressor side
            t.side = Side::undetermined;
            ++_counters.trades;
        }
        return true;
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc) && ./build/tests/test_algoseek_source_test
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/algoseek/algoseek_data_source.cpp src/tests/algoseek_source_test.cpp
git commit -m "feat(algoseek): map auction prints, drop administrative rows, filter by exchange"
```

---

### Task 5: Row validation, ordering guard, and the EOF summary

**Files:**
- Modify: `src/quarkbot/algoseek/algoseek_data_source.hpp` (three new members)
- Modify: `src/quarkbot/algoseek/algoseek_data_source.cpp`
- Modify: `src/tests/algoseek_source_test.cpp`

**Interfaces:**
- Consumes: `quarkbot::AlgoseekDataSource` (Tasks 3–4).
- Produces: no new public symbols. Adds the private members `std::string _first_ticker`, `std::chrono::system_clock::time_point _last_time` and `void check_required_fields() const`.

- [ ] **Step 1: Write the failing test**

Add to `src/tests/algoseek_source_test.cpp`:

```cpp
static void test_row_errors() {
    std::cout << "--- test_row_errors" << std::endl;

    // missing header column
    {
        const std::string path = "/tmp/test_algoseek_badheader.csv.gz";
        write_gz(path, "Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange\n"
                       "20230609,10:00:00.000000000,TRADE,IBM,47.60,100,NYSE\n");
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
                std::string_view(e.what()).find("Conditions") != std::string_view::npos,
                AlgoseekDataSource src(parse_algoseek_spec(path)));
    }

    // nine fields: a Price rewritten with a comma decimal separator shifts the
    // columns, so Conditions ends up holding the exchange name
    {
        const std::string path = "/tmp/test_algoseek_shifted.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:00.000000000,TRADE,IBM,242,54,6,EDGX,80002000\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
                std::string_view(e.what()).find("Conditions") != std::string_view::npos,
                src(ev));
    }

    // Conditions that is not hexadecimal
    {
        const std::string path = "/tmp/test_algoseek_badcond.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:00.000000000,TRADE,IBM,47.60,100,NYSE,zzzzzzzz\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }

    // an empty required field
    {
        const std::string path = "/tmp/test_algoseek_empty.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:00.000000000,TRADE,IBM,,100,NYSE,00000001\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
                std::string_view(e.what()).find("Price") != std::string_view::npos,
                src(ev));
    }

    // a malformed timestamp
    {
        const std::string path = "/tmp/test_algoseek_badtime.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:00,TRADE,IBM,47.60,100,NYSE,00000001\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }

    // a decreasing timestamp would corrupt the merged timeline
    {
        const std::string path = "/tmp/test_algoseek_unordered.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:01.000000000,TRADE,IBM,47.60,100,NYSE,00000001\n"
                "20230609,10:00:00.000000000,TRADE,IBM,47.61,100,NYSE,00000001\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }

    // a changing Ticker would let a symbol override merge two instruments
    {
        const std::string path = "/tmp/test_algoseek_twotickers.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:00.000000000,TRADE,IBM,47.60,100,NYSE,00000001\n"
                "20230609,10:00:01.000000000,TRADE,MSFT,47.61,100,NYSE,00000001\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
                std::string_view(e.what()).find("MSFT") != std::string_view::npos,
                src(ev));
    }

    // a nonexistent file fails when the source is constructed
    CHECK_EXCEPTION(std::runtime_error,
            AlgoseekDataSource src(parse_algoseek_spec("/tmp/does_not_exist_algoseek.csv.gz")));
}
```

Call `test_row_errors();` from `main` after `test_exchange_filter_and_symbol();`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc) && ./build/tests/test_algoseek_source_test
```

Expected: FAIL at the empty-required-field case, reporting
`FAILED: throw std::runtime_error`. An empty `Price` currently parses to
`Decimal` zero and is swallowed by the `zero_price` skip added in Task 4, so
`src(ev)` returns `false` instead of throwing.

Five of the eight cases already pass before this task, which is expected: the
missing header column throws from `map_columns` (Task 3), the shifted nine-field
row and the non-hex `Conditions` both throw from `parse_conditions` (Task 3), and
the malformed timestamp throws from `parse_local_time` (Task 3). The nonexistent
file throws from `init_source` (Task 3). The three cases this task must fix are
the empty field, the decreasing timestamp and the changing `Ticker`.

- [ ] **Step 3: Add the new members to the header**

In `src/quarkbot/algoseek/algoseek_data_source.hpp`, inside the `protected`
section, add after `bool _eof = false;`:

```cpp
    ///Ticker seen on the first row, used to reject concatenated files
    std::string _first_ticker;
    ///timestamp of the last emitted event, used to reject unordered input
    std::chrono::system_clock::time_point _last_time = {};
```

and declare the new helper next to the other private helpers:

```cpp
    ///throw unless every column of the current row carries a value
    void check_required_fields() const;
```

- [ ] **Step 4: Implement validation and extend the summary**

In `src/quarkbot/algoseek/algoseek_data_source.cpp`, add the new helper next to
`parse_conditions`:

```cpp
void AlgoseekDataSource::check_required_fields() const {
    auto check = [&](std::string_view name, const std::string &value) {
        if (value.empty()) row_error(std::format("column {} is empty", name));
    };
    check("Date", _row.date);
    check("Timestamp", _row.timestamp);
    check("EventType", _row.event_type);
    check("Ticker", _row.ticker);
    check("Price", _row.price);
    check("Quantity", _row.quantity);
    check("Exchange", _row.exchange);
    check("Conditions", _row.conditions);
}
```

In `operator()`, insert the per-row validation immediately after `++_line;`, before
the `TRADE CANCELLED` check:

```cpp
        check_required_fields();
        if (_first_ticker.empty()) {
            _first_ticker = _row.ticker;
        } else if (_row.ticker != _first_ticker) {
            row_error(std::format("Ticker changed from '{}' to '{}'; one file must "
                    "hold one ticker", _first_ticker, _row.ticker));
        }
```

and insert the ordering guard immediately after `auto time = _tz.to_sys(parse_local_time());`:

```cpp
        if (time < _last_time) {
            row_error(std::format("timestamp {} is earlier than the previous event at {}; "
                    "MergedDataSource requires each source to be ordered", time, _last_time));
        }
        _last_time = time;
```

Replace `log_summary` with the full version:

```cpp
void AlgoseekDataSource::log_summary() const {
    bool suspicious = _counters.unknown_event > 0
            || (_counters.trades == 0 && _counters.auctions == 0);
    auto level = suspicious ? LogLevel::warning : LogLevel::info;
    logOutput(level, "Algoseek source {}: {} trades, {} auctions; skipped: "
            "{} cancelled, {} unknown event, {} other exchange, {} zero quantity, "
            "{} zero price, {} official print",
            _spec.file.string(), _counters.trades, _counters.auctions,
            _counters.cancelled, _counters.unknown_event, _counters.filtered_exchange,
            _counters.zero_qty, _counters.zero_price, _counters.official_print);
}
```

A source that emitted nothing at all is reported as a warning because that is the
only visible symptom of a misspelled `exchange` value. Cancellations are not part
of the warning condition: they are routine in real exports (6 of the 9 reference
files contain them), so warning on `cancelled > 0` would fire on most healthy
files and drown out the two conditions that actually indicate a problem. They are
still counted and reported in the summary line, just at whichever level the other
counters set.

- [ ] **Step 5: Run the test to verify it passes**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc) && ./build/tests/test_algoseek_source_test
```

Expected: PASS. Both formatting constructs used here were verified against
g++-14: `std::format` renders a `system_clock::time_point` as
`1970-01-01 00:00:00.123456789`, and `logOutput` accepts a runtime `LogLevel`
together with `std::string` and `std::uint64_t` arguments.

- [ ] **Step 6: Commit**

```bash
git add src/quarkbot/algoseek/ src/tests/algoseek_source_test.cpp
git commit -m "feat(algoseek): validate rows, reject unordered input, log a skip summary"
```

---

### Task 6: Regression against real export files

**Files:**
- Create: `src/tests/data/20240418_NASDAQ_DHIL.csv.gz` (copied, 7 KB)
- Create: `src/tests/data/20230609_BIPC.csv.gz` (copied, 59 KB)
- Modify: `src/tests/algoseek_source_test.cpp`

**Interfaces:**
- Consumes: `quarkbot::AlgoseekDataSource` (Tasks 3–5), the `TEST_DATA_PATH` macro (Task 1), `drain` (Task 4).
- Produces: nothing new.

All expected counts below were produced by running this exact pipeline over these
two files; they are measurements, not estimates.

- [ ] **Step 1: Copy the fixtures**

```bash
mkdir -p src/tests/data
cp /home/ondra/vscode/AlgoseekUtils/src/test/resources/20240418_NASDAQ_DHIL.csv.gz src/tests/data/
cp /home/ondra/vscode/AlgoseekUtils/src/test/resources/20230609_BIPC.csv.gz src/tests/data/
ls -l src/tests/data/
```

Expected: two files, roughly 7 KB and 59 KB.

- [ ] **Step 2: Write the failing test**

Add to `src/tests/algoseek_source_test.cpp`:

```cpp
///count emitted events by kind
struct EventCounts {
    std::size_t total = 0;
    std::size_t trades = 0;
    std::size_t opening = 0;
    std::size_t closing = 0;
    std::size_t unscheduled = 0;
};

static EventCounts count_events(const std::string &spec) {
    AlgoseekDataSource src(parse_algoseek_spec(spec));
    EventCounts c;
    for (const auto &ev: drain(src)) {
        ++c.total;
        if (std::holds_alternative<Trade>(ev.data)) {
            ++c.trades;
        } else if (std::holds_alternative<Auction>(ev.data)) {
            switch (std::get<Auction>(ev.data).auction_type) {
                case AuctionType::opening: ++c.opening; break;
                case AuctionType::closing: ++c.closing; break;
                case AuctionType::unscheduled: ++c.unscheduled; break;
                default: break;
            }
        }
    }
    return c;
}

static void test_real_files() {
    std::cout << "--- test_real_files" << std::endl;

    const std::string dhil = std::string(TEST_DATA_PATH) + "/20240418_NASDAQ_DHIL.csv.gz";
    const std::string bipc = std::string(TEST_DATA_PATH) + "/20230609_BIPC.csv.gz";
    //the same parameter as the only one, and appended to another
    const std::string tz_only = "?tzone=America/New_York";
    const std::string tz_more = "&tzone=America/New_York";

    // DHIL, 594 data rows: 588 trades + 2 auctions emitted, 4 official prints dropped
    {
        auto c = count_events(dhil + tz_only);
        CHECK_EQUAL(c.total, std::size_t(590));
        CHECK_EQUAL(c.trades, std::size_t(588));
        CHECK_EQUAL(c.opening, std::size_t(1));
        CHECK_EQUAL(c.closing, std::size_t(1));
        CHECK_EQUAL(c.unscheduled, std::size_t(0));
    }
    // restricted to NASDAQ: 321 rows belong to other venues
    {
        auto c = count_events(dhil + "?exchange=NASDAQ" + tz_more);
        CHECK_EQUAL(c.total, std::size_t(271));
        CHECK_EQUAL(c.trades, std::size_t(269));
        CHECK_EQUAL(c.opening, std::size_t(1));
        CHECK_EQUAL(c.closing, std::size_t(1));
    }

    // BIPC, 5627 data rows: 9 official prints and 3 zero quantity rows dropped
    {
        auto c = count_events(bipc + tz_only);
        CHECK_EQUAL(c.total, std::size_t(5615));
        CHECK_EQUAL(c.trades, std::size_t(5613));
        CHECK_EQUAL(c.opening, std::size_t(1));
        CHECK_EQUAL(c.closing, std::size_t(1));
    }
    // restricted to NYSE: 4737 rows belong to other venues, 5 official prints
    // and the 3 zero quantity rows remain on NYSE and are dropped
    {
        auto c = count_events(bipc + "?exchange=NYSE" + tz_more);
        CHECK_EQUAL(c.total, std::size_t(882));
        CHECK_EQUAL(c.trades, std::size_t(880));
        CHECK_EQUAL(c.opening, std::size_t(1));
        CHECK_EQUAL(c.closing, std::size_t(1));
    }

    // the auctions carry the expected values, converted from Eastern to UTC.
    // The closing print 47.92 x 23455 appears four more times in this file as an
    // official close, the last at 19:00; exactly one closing auction must survive.
    {
        AlgoseekDataSource src(parse_algoseek_spec(bipc + "?exchange=NYSE" + tz_more));
        const Auction *opening = nullptr;
        const Auction *closing = nullptr;
        auto evs = drain(src);
        for (const auto &ev: evs) {
            if (!std::holds_alternative<Auction>(ev.data)) continue;
            const auto &a = std::get<Auction>(ev.data);
            if (a.auction_type == AuctionType::opening) opening = &a;
            if (a.auction_type == AuctionType::closing) closing = &a;
        }
        CHECK(opening != nullptr);
        CHECK(opening->price == Decimal::from_string("47.58"));
        CHECK(opening->quantity == Decimal::from_string("4000"));
        CHECK(opening->time == mk_utc("2023-06-09 13:30:00.791480832"));
        CHECK(closing != nullptr);
        CHECK(closing->price == Decimal::from_string("47.92"));
        CHECK(closing->quantity == Decimal::from_string("23455"));
        CHECK(closing->time == mk_utc("2023-06-09 20:00:02.164273920"));

        // the real files come out ordered, which MergedDataSource depends on.
        // One assertion, not one per element: the harness prints a line per
        // CHECK, and a per-element loop would emit 881 identical lines.
        CHECK(std::is_sorted(evs.begin(), evs.end(),
                [](const BacktestEvent &a, const BacktestEvent &b){
                    return a.time < b.time; }));
    }
}
```

Call `test_real_files();` from `main` after `test_row_errors();`.

- [ ] **Step 3: Run the test to verify it passes**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc) && ./build/tests/test_algoseek_source_test
```

Expected: PASS. This task has no production code change — the fixtures confirm
the behaviour built in Tasks 3–5. If a count is off, the pipeline has a defect;
do not adjust the expected numbers.

- [ ] **Step 4: Commit**

```bash
git add src/tests/data/ src/tests/algoseek_source_test.cpp
git commit -m "test(algoseek): regression against two real export files"
```

---

### Task 7: Wire the source into the INI configuration

**Files:**
- Modify: `src/quarkbot/backtest/config_datasource.cpp` (includes, `walk`, a new `add_algoseek`)
- Modify: `src/quarkbot/backtest/config_datasource.hpp` (doc comment)
- Modify: `src/tests/algoseek_source_test.cpp`

**Interfaces:**
- Consumes: `quarkbot::parse_algoseek_spec`, `quarkbot::AlgoseekDataSource`, `quarkbot::configure_datasources`.
- Produces: the `algoseek=` key in the `[data-source]` section.

- [ ] **Step 1: Write the failing test**

Add to `src/tests/algoseek_source_test.cpp`:

```cpp
#include "quarkbot/backtest/config_datasource.hpp"
#include <filesystem>
#include <fstream>

static void test_config_wiring() {
    std::cout << "--- test_config_wiring" << std::endl;

    auto dir = std::filesystem::temp_directory_path() / "algoseek_cfg_test";
    std::filesystem::create_directories(dir);

    // the data file sits next to the config, referenced by a relative path
    write_gz((dir / "IBM.csv.gz").string(), std::string(ALGOSEEK_HEADER) +
        "20230609,10:00:00.000000000,TRADE,IBM,47.60,100,NASDAQ,00000001\n"
        "20230609,10:00:01.000000000,TRADE,IBM,47.61,200,ARCA,00000001\n"
        "20230609,10:00:02.000000000,TRADE,IBM,47.62,300,NASDAQ,00000001\n");

    {
        std::ofstream ini(dir / "backtest.ini");
        ini << "[data-source]\n"
               "algoseek=IBM.csv.gz?exchange=NASDAQ&tzone=America/New_York\n";
    }

    auto ds = configure_datasources(dir / "backtest.ini");
    BacktestEvent ev;
    CHECK(ds(ev));
    CHECK_EQUAL(ev.symbol, std::string("IBM"));
    CHECK(ev.time == mk_utc("2023-06-09 14:00:00"));
    CHECK(std::get<Trade>(ev.data).price == Decimal::from_string("47.60"));
    CHECK(ds(ev));
    CHECK(std::get<Trade>(ev.data).price == Decimal::from_string("47.62"));
    CHECK(!ds(ev));

    // a bad spec fails while the configuration is being read
    {
        std::ofstream ini(dir / "bad.ini");
        ini << "[data-source]\n"
               "algoseek=IBM.csv.gz?bogus=1\n";
    }
    CHECK_EXCEPTION(std::runtime_error, configure_datasources(dir / "bad.ini"));

    std::filesystem::remove_all(dir);
}
```

Call `test_config_wiring();` from `main` after `test_real_files();`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --target test_algoseek_source_test -j$(nproc) && ./build/tests/test_algoseek_source_test
```

Expected: FAIL with the source throwing
`Unknown key algoseek in config .../backtest.ini`.

- [ ] **Step 3: Add the dispatch**

In `src/quarkbot/backtest/config_datasource.cpp`, add the include next to the
other component includes:

```cpp
#include "../algoseek/algoseek_data_source.hpp"
```

In `SourceCollector::walk`, add the new key to the `[data-source]` branch, after
the `lseg`/`trth` line. Note that this key receives the raw value rather than a
joined path:

```cpp
                else if (row.key == "algoseek") add_algoseek(root, row.value);
```

Add the method next to `add_trth`:

```cpp
    ///the value carries a query string, so the path can only be joined after parsing
    void add_algoseek(const std::filesystem::path &root, std::string_view value) {
        auto spec = parse_algoseek_spec(value);
        spec.file = root / spec.file;
        sources.push_back(AlgoseekDataSource(std::move(spec)));
    }
```

- [ ] **Step 4: Document the key**

In `src/quarkbot/backtest/config_datasource.hpp`, extend the list of keys in the
doc comment. Keep the explanation on its own lines: `IniReader` only strips a
comment when the line starts with `;`, so a trailing comment would end up inside
the value.

```
[data-source]
quarkbot=file.gz     ;quarkbot replay format  - gzip + CSV
tardis=file.gz       ;tardis trades and quotes gzip
lseg=file.gz         ;lseg trades, quotes, auctions gzip
trth=file.gz         ;trth trades, quotes, auctions gzip
algoseek=file.csv.gz?exchange=NASDAQ&tzone=America/New_York&symbol=IBM.NASDAQ

The algoseek key reads an Algoseek US equity "Trades Only" export and produces
Trade and final Auction events. Its value is a file path with an optional query
string; all three parameters are optional:

  exchange - emit only rows of this venue, matched verbatim against the
             Exchange column. Without it every venue is replayed, including
             the off-exchange prints reported under FINRA.
  tzone    - IANA name of the zone the file's wall clock timestamps are in.
             Defaults to UTC; real exports are in America/New_York.
  symbol   - symbol reported on events, instead of the Ticker column. Needed
             when the same ticker is replayed from two venues, which would
             otherwise collide on one instrument.

Do not write a trailing comment after a value: only a line starting with ';'
or '#' is treated as a comment, so it would become part of the value.
```

- [ ] **Step 5: Run the whole suite**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build
```

Expected: all tests pass, including `tests/algoseek_source_test`.

- [ ] **Step 6: Commit**

```bash
git add src/quarkbot/backtest/config_datasource.cpp src/quarkbot/backtest/config_datasource.hpp src/tests/algoseek_source_test.cpp
git commit -m "feat(algoseek): wire the source into the data-source configuration"
```

---

## Self-Review

**Spec coverage**

| Spec section | Task |
|---|---|
| Configuration syntax, three parameters, unknown key is an error | 1 |
| `exchange` matched verbatim, values with spaces | 1, 4 |
| Timezone conversion, DST caching, UTC default | 1, 2 |
| Component layout, CMake wiring, no new option | 1, 2, 3 |
| gzip CSV reading, header validation | 3 |
| `TRADE` and `TRADE NB` both emitted | 3 |
| `Date`/`Timestamp` parse format | 3 |
| Auction mapping for bits 6/7/8, `final = true`, defaults | 4 |
| Auction precedence over official prints | 4 |
| Official open/close, zero quantity, zero price dropped | 4 |
| `TRADE CANCELLED` dropped without retraction | 4 |
| Exchange filter, `symbol` override | 4 |
| `Side::undetermined` | 3 |
| Required-field, Conditions, date/time row errors | 5 |
| Monotonicity guard | 5, 6 |
| Ticker guard | 5 |
| EOF summary counters, warning on zero events | 5 |
| Real-data regression with exact counts | 6 |
| `algoseek=` config key and its documentation | 7 |

Spec items intentionally not implemented as code: the "Notes for implementation"
section only records that the gzip reader boilerplate is duplicated a third time
rather than extracted, which Task 3 follows by copying the `trth` pattern.

**Type consistency**

- `parse_algoseek_spec` returns `AlgoseekSpec` by value in every task; the
  `AlgoseekDataSource` constructor takes it by value and is always called with a
  moved or temporary spec.
- `LocalTimeConverter::to_sys` takes `std::chrono::local_time<std::chrono::nanoseconds>`
  and returns `std::chrono::system_clock::time_point` in Tasks 2, 3 and 5.
- `has_flag(std::uint32_t, AlgoseekTradeFlag)` is used with the `std::uint32_t`
  returned by `parse_conditions` in Tasks 4 and 5.
- Counter field names (`trades`, `auctions`, `cancelled`, `unknown_event`,
  `filtered_exchange`, `zero_qty`, `zero_price`, `official_print`) match between
  the `Counters` struct in Task 3 and its uses in Tasks 4 and 5.
- Test helpers `mk_local`, `mk_utc` (Task 2), `write_gz`, `ALGOSEEK_HEADER`
  (Task 3) and `drain` (Task 4) are defined once and reused by later tasks.
