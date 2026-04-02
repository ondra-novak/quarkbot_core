# Backtest Replay Parser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone C++ library that writes/reads a binary replay file format (`.qkrp`) and converts CSV/JSON market data into it.

**Architecture:** A central binary format with a fixed header + sequential length-prefixed records; a `ReplayWriter` that serialises events and emits a companion `.qkrp.json` on close; a `ReplayReader` that deserialises events via `next()`; three converter classes that parse external formats and drive the writer.

**Tech Stack:** C++23 stdlib (base library), nlohmann/json v3.11.3 (JSON converter only), CMake 3.22+ with FetchContent, ctest for tests.

---

## File Map

| File | Responsibility |
|---|---|
| `src/backtest/CMakeLists.txt` | Standalone + subdirectory build root |
| `src/backtest/tests/CMakeLists.txt` | Test targets + ctest registration |
| `src/backtest/replay_format.hpp` | Wire-format packed structs, `EventType` enum, public event types, `Event` variant |
| `src/backtest/replay_writer.hpp` | `ReplayWriter` declaration |
| `src/backtest/replay_writer.cpp` | `ReplayWriter` implementation + companion JSON |
| `src/backtest/replay_reader.hpp` | `ReplayReader` declaration |
| `src/backtest/replay_reader.cpp` | `ReplayReader` implementation |
| `src/backtest/converters/ifc.hpp` | `IConverter` interface |
| `src/backtest/converters/csv_parser.hpp` | Shared CSV row parser + timestamp parser |
| `src/backtest/converters/csv_ohlc.hpp/.cpp` | OHLCV CSV → `ClosedBarEvent` |
| `src/backtest/converters/csv_tick.hpp/.cpp` | Tick CSV → `TradeEvent` + `QuoteEvent` |
| `src/backtest/converters/json_exchange.hpp/.cpp` | NDJSON/JSON array → any event |
| `src/backtest/tests/test_helpers.hpp` | `CHECK` / `CHECK_EQ` / `CHECK_NEAR` macros |
| `src/backtest/tests/test_reader_writer.cpp` | Writer + reader round-trip tests |
| `src/backtest/tests/test_csv_ohlc.cpp` | CsvOhlcConverter tests |
| `src/backtest/tests/test_csv_tick.cpp` | CsvTickConverter tests |
| `src/backtest/tests/test_json_exchange.cpp` | JsonExchangeConverter tests |

---

## Task 1: CMake scaffold + compile check

**Files:**
- Create: `src/backtest/CMakeLists.txt`
- Create: `src/backtest/tests/CMakeLists.txt`

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p /home/ondra/workspace/trading_interface/src/backtest/converters
mkdir -p /home/ondra/workspace/trading_interface/src/backtest/tests
```

- [ ] **Step 2: Write `src/backtest/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.22)
project(replay_parser CXX)
set(CMAKE_CXX_STANDARD 23)

include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(replay_parser STATIC
    replay_writer.cpp
    replay_reader.cpp
    converters/csv_ohlc.cpp
    converters/csv_tick.cpp
    converters/json_exchange.cpp
)
target_include_directories(replay_parser
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(replay_parser PRIVATE nlohmann_json::nlohmann_json)

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 3: Create empty stub source files so CMake can link**

```bash
cd /home/ondra/workspace/trading_interface/src/backtest
touch replay_writer.cpp replay_reader.cpp
touch converters/csv_ohlc.cpp converters/csv_tick.cpp converters/json_exchange.cpp
```

- [ ] **Step 4: Write `src/backtest/tests/CMakeLists.txt`**

```cmake
foreach(test_name test_reader_writer test_csv_ohlc test_csv_tick test_json_exchange)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE replay_parser nlohmann_json::nlohmann_json)
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
```

- [ ] **Step 5: Create empty test stub files**

```bash
cd /home/ondra/workspace/trading_interface/src/backtest/tests
for f in test_reader_writer test_csv_ohlc test_csv_tick test_json_exchange; do
    echo "int main(){}" > ${f}.cpp
done
```

- [ ] **Step 6: Verify the build configures and compiles**

```bash
cmake -S /home/ondra/workspace/trading_interface/src/backtest \
      -B /tmp/replay_parser_build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/replay_parser_build
```

Expected: build succeeds, four test executables created.

- [ ] **Step 7: Commit**

```bash
cd /home/ondra/workspace/trading_interface
git add src/backtest/
git commit -m "feat(backtest): CMake scaffold for replay_parser library"
```

---

## Task 2: Wire format and event types (`replay_format.hpp`)

**Files:**
- Create: `src/backtest/replay_format.hpp`

- [ ] **Step 1: Write `src/backtest/replay_format.hpp`**

```cpp
#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace quarkbot::replay {

using tp_t = std::chrono::system_clock::time_point;

// ── File format constants ─────────────────────────────────────────────────────

inline constexpr std::array<char, 8> FILE_MAGIC = {'Q','K','R','P','L','A','Y','\0'};
inline constexpr uint8_t FILE_VERSION = 1;

enum class EventType : uint8_t {
    trade              = 0x01,
    quote              = 0x02,
    closed_bar         = 0x03,
    orderbook_snapshot = 0x04,
    orderbook_delta    = 0x05,
};

inline constexpr const char* event_type_name(EventType t) noexcept {
    switch (t) {
        case EventType::trade:              return "trade";
        case EventType::quote:              return "quote";
        case EventType::closed_bar:         return "closed_bar";
        case EventType::orderbook_snapshot: return "orderbook_snapshot";
        case EventType::orderbook_delta:    return "orderbook_delta";
        default:                            return "unknown";
    }
}

// ── Wire-format packed structs (little-endian, IEEE 754) ──────────────────────

#pragma pack(push, 1)

struct FileHeader {
    char    magic[8];
    uint8_t version;
    uint8_t reserved[3];
};
static_assert(sizeof(FileHeader) == 12);

struct RecordHeader {
    uint32_t record_len;    // total bytes of record, inclusive
    uint8_t  type;
    int64_t  timestamp_us;  // microseconds since Unix epoch
    uint16_t symbol_len;
};
static_assert(sizeof(RecordHeader) == 15);

struct TradePayload     { double price, size; };
struct QuotePayload     { double bid, bid_size, ask, ask_size; };
struct ClosedBarPayload { double open, high, low, close, volume; uint32_t interval_sec; };
struct OBLevelWire      { double price, size; uint8_t side; }; // side: 0=bid, 1=ask
struct OBSnapshotHeader { uint16_t count; };
struct OBDeltaPayload   { double price, size; uint8_t side; };

#pragma pack(pop)

// ── Public event types ────────────────────────────────────────────────────────

struct TradeEvent {
    std::string_view symbol;
    tp_t time;
    double price, size;
};

struct QuoteEvent {
    std::string_view symbol;
    tp_t time;
    double bid, bid_size, ask, ask_size;
};

struct ClosedBarEvent {
    std::string_view symbol;
    tp_t time;
    double open, high, low, close, volume;
    uint32_t interval_sec;
};

struct OBLevel {
    double price, size;
    bool is_bid;
};

struct OrderBookSnapshotEvent {
    std::string_view symbol;
    tp_t time;
    std::span<const OBLevel> levels;
};

struct OrderBookDeltaEvent {
    std::string_view symbol;
    tp_t time;
    double price, size;  // size == 0.0 means remove level
    bool is_bid;
};

using Event = std::variant<
    TradeEvent, QuoteEvent, ClosedBarEvent,
    OrderBookSnapshotEvent, OrderBookDeltaEvent
>;

} // namespace quarkbot::replay
```

- [ ] **Step 2: Verify it compiles (adds include to the compile-test stub)**

Edit `src/backtest/tests/test_reader_writer.cpp` temporarily:
```cpp
#include "replay_format.hpp"
using namespace quarkbot::replay;
static_assert(sizeof(FileHeader) == 12);
static_assert(sizeof(RecordHeader) == 15);
int main() {}
```

```bash
cmake --build /tmp/replay_parser_build --target test_reader_writer
```

Expected: compiles with no errors.

- [ ] **Step 3: Commit**

```bash
cd /home/ondra/workspace/trading_interface
git add src/backtest/replay_format.hpp src/backtest/tests/test_reader_writer.cpp
git commit -m "feat(backtest): wire format constants and public event types"
```

---

## Task 3: ReplayWriter

**Files:**
- Create: `src/backtest/replay_writer.hpp`
- Modify: `src/backtest/replay_writer.cpp`
- Test: `src/backtest/tests/test_reader_writer.cpp`

- [ ] **Step 1: Write `src/backtest/tests/test_helpers.hpp`**

```cpp
#pragma once
#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
        std::exit(1); \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (!(_a == _b)) { \
        std::cerr << __FILE__ << ":" << __LINE__ \
                  << ": CHECK_EQ failed: " #a " == " #b "\n"; \
        std::exit(1); \
    } \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    auto _a = (double)(a); auto _b = (double)(b); \
    if (std::abs(_a - _b) > (eps)) { \
        std::cerr << __FILE__ << ":" << __LINE__ \
                  << ": CHECK_NEAR failed: " #a " ~= " #b \
                  << " (got " << _a << " vs " << _b << ")\n"; \
        std::exit(1); \
    } \
} while (0)
```

- [ ] **Step 2: Write the failing test in `src/backtest/tests/test_reader_writer.cpp`**

```cpp
#include "replay_format.hpp"
#include "replay_writer.hpp"
#include "test_helpers.hpp"
#include <chrono>
#include <cstring>
#include <sstream>

using namespace quarkbot::replay;
using namespace std::chrono;

static tp_t make_tp(int64_t us) {
    return tp_t{microseconds{us}};
}

static void test_writer_magic() {
    std::ostringstream oss;
    {
        ReplayWriter w(oss);
        w.write(TradeEvent{"BTCUSDT", make_tp(1'704'067'200'000'000LL), 42000.5, 0.1});
        w.close();
    }
    const std::string data = oss.str();
    // header is 12 bytes minimum
    CHECK(data.size() >= 12);
    CHECK(data[0] == 'Q');
    CHECK(data[1] == 'K');
    CHECK(data[2] == 'R');
    CHECK(data[3] == 'P');
    CHECK(data[4] == 'L');
    CHECK(data[5] == 'A');
    CHECK(data[6] == 'Y');
    CHECK(data[7] == '\0');
    CHECK(static_cast<uint8_t>(data[8]) == 1); // version
}

static void test_writer_record_size() {
    std::ostringstream oss;
    {
        ReplayWriter w(oss);
        w.write(TradeEvent{"BTC", make_tp(1'000'000'000'000'000LL), 100.0, 1.0});
        w.close();
    }
    const std::string data = oss.str();
    // expected: 12 (header) + 15 (RecordHeader) + 3 (symbol "BTC") + 16 (two doubles)
    CHECK_EQ(data.size(), 12u + 15u + 3u + 16u);
}

int main() {
    test_writer_magic();
    test_writer_record_size();
    std::cout << "test_writer: PASS\n";
}
```

- [ ] **Step 3: Run test — verify it fails**

```bash
cmake --build /tmp/replay_parser_build --target test_reader_writer 2>&1 | tail -5
```

Expected: compile error — `replay_writer.hpp` not found.

- [ ] **Step 4: Write `src/backtest/replay_writer.hpp`**

```cpp
#pragma once
#include "replay_format.hpp"
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <string>

namespace quarkbot::replay {

class ReplayWriter {
public:
    explicit ReplayWriter(std::string_view path);   // opens .qkrp, writes .qkrp.json on close
    explicit ReplayWriter(std::ostream &stream);    // stream-only, no companion file
    ~ReplayWriter();

    void write(const Event &event);
    void close();   // flushes binary + writes companion JSON (if path-based); idempotent

private:
    void write_file_header();
    void write_bytes(const void *data, std::size_t n);
    void write_record(EventType type, int64_t ts_us, std::string_view symbol,
                      const void *payload, std::size_t payload_size);
    void write_ob_snapshot(int64_t ts_us, std::string_view symbol,
                           std::span<const OBLevel> levels);
    void write_companion_json(const std::string &path);

    static int64_t to_us(tp_t tp);
    static std::string format_iso(int64_t us);

    std::ofstream              _owned_file;
    std::ostream              *_out    = nullptr;
    std::optional<std::string> _path;
    bool                       _closed = false;

    uint64_t                          _total_events = 0;
    int64_t                           _time_min = std::numeric_limits<int64_t>::max();
    int64_t                           _time_max = std::numeric_limits<int64_t>::min();
    std::set<std::string, std::less<>> _symbols;
    std::set<std::string, std::less<>> _event_types;
};

} // namespace quarkbot::replay
```

- [ ] **Step 5: Write `src/backtest/replay_writer.cpp`**

```cpp
#include "replay_writer.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace quarkbot::replay {

ReplayWriter::ReplayWriter(std::string_view path)
    : _owned_file(std::string(path), std::ios::binary | std::ios::trunc)
    , _path(std::string(path))
{
    if (!_owned_file)
        throw std::runtime_error("Cannot open for writing: " + std::string(path));
    _out = &_owned_file;
    write_file_header();
}

ReplayWriter::ReplayWriter(std::ostream &stream)
    : _out(&stream)
{
    write_file_header();
}

ReplayWriter::~ReplayWriter() {
    if (!_closed) close();
}

void ReplayWriter::write_file_header() {
    FileHeader hdr{};
    std::copy(FILE_MAGIC.begin(), FILE_MAGIC.end(), hdr.magic);
    hdr.version = FILE_VERSION;
    write_bytes(&hdr, sizeof(hdr));
}

void ReplayWriter::write_bytes(const void *data, std::size_t n) {
    _out->write(static_cast<const char *>(data), static_cast<std::streamsize>(n));
    if (!*_out) throw std::runtime_error("Write failed");
}

void ReplayWriter::write_record(EventType type, int64_t ts_us,
                                 std::string_view symbol,
                                 const void *payload, std::size_t payload_size) {
    const uint16_t sym_len = static_cast<uint16_t>(symbol.size());
    const uint32_t rec_len = static_cast<uint32_t>(
        sizeof(RecordHeader) + sym_len + payload_size);

    RecordHeader hdr{};
    hdr.record_len   = rec_len;
    hdr.type         = static_cast<uint8_t>(type);
    hdr.timestamp_us = ts_us;
    hdr.symbol_len   = sym_len;

    write_bytes(&hdr,          sizeof(hdr));
    write_bytes(symbol.data(), sym_len);
    write_bytes(payload,       payload_size);
}

void ReplayWriter::write_ob_snapshot(int64_t ts_us, std::string_view symbol,
                                      std::span<const OBLevel> levels) {
    const uint16_t sym_len = static_cast<uint16_t>(symbol.size());
    const uint16_t count   = static_cast<uint16_t>(levels.size());
    const uint32_t rec_len = static_cast<uint32_t>(
        sizeof(RecordHeader) + sym_len +
        sizeof(OBSnapshotHeader) + count * sizeof(OBLevelWire));

    RecordHeader rhdr{};
    rhdr.record_len   = rec_len;
    rhdr.type         = static_cast<uint8_t>(EventType::orderbook_snapshot);
    rhdr.timestamp_us = ts_us;
    rhdr.symbol_len   = sym_len;

    write_bytes(&rhdr,         sizeof(rhdr));
    write_bytes(symbol.data(), sym_len);

    OBSnapshotHeader sh{count};
    write_bytes(&sh, sizeof(sh));

    for (const auto &lvl : levels) {
        OBLevelWire w{lvl.price, lvl.size, static_cast<uint8_t>(lvl.is_bid ? 0 : 1)};
        write_bytes(&w, sizeof(w));
    }
}

int64_t ReplayWriter::to_us(tp_t tp) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
}

void ReplayWriter::write(const Event &event) {
    ++_total_events;

    std::visit([this](const auto &e) {
        const int64_t ts = to_us(e.time);
        _time_min = std::min(_time_min, ts);
        _time_max = std::max(_time_max, ts);
        _symbols.emplace(e.symbol);

        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, TradeEvent>) {
            _event_types.emplace("trade");
            TradePayload p{e.price, e.size};
            write_record(EventType::trade, ts, e.symbol, &p, sizeof(p));

        } else if constexpr (std::is_same_v<T, QuoteEvent>) {
            _event_types.emplace("quote");
            QuotePayload p{e.bid, e.bid_size, e.ask, e.ask_size};
            write_record(EventType::quote, ts, e.symbol, &p, sizeof(p));

        } else if constexpr (std::is_same_v<T, ClosedBarEvent>) {
            _event_types.emplace("closed_bar");
            ClosedBarPayload p{e.open, e.high, e.low, e.close, e.volume, e.interval_sec};
            write_record(EventType::closed_bar, ts, e.symbol, &p, sizeof(p));

        } else if constexpr (std::is_same_v<T, OrderBookSnapshotEvent>) {
            _event_types.emplace("orderbook_snapshot");
            write_ob_snapshot(ts, e.symbol, e.levels);

        } else if constexpr (std::is_same_v<T, OrderBookDeltaEvent>) {
            _event_types.emplace("orderbook_delta");
            OBDeltaPayload p{e.price, e.size,
                             static_cast<uint8_t>(e.is_bid ? 0 : 1)};
            write_record(EventType::orderbook_delta, ts, e.symbol, &p, sizeof(p));
        }
    }, event);
}

std::string ReplayWriter::format_iso(int64_t us) {
    const time_t secs = static_cast<time_t>(us / 1'000'000);
    const int    ms   = static_cast<int>((us % 1'000'000) / 1000);
    struct tm utc{};
    gmtime_r(&secs, &utc);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
        utc.tm_hour, utc.tm_min, utc.tm_sec, ms);
    return buf;
}

void ReplayWriter::write_companion_json(const std::string &path) {
    std::ofstream f(path + ".json");
    if (!f) return; // best-effort

    f << "{\n";
    f << "  \"version\": 1,\n";
    f << "  \"total_events\": " << _total_events << ",\n";
    if (_total_events > 0) {
        f << "  \"time_start\": \"" << format_iso(_time_min) << "\",\n";
        f << "  \"time_end\": \""   << format_iso(_time_max) << "\",\n";
    }
    f << "  \"symbols\": [";
    bool first = true;
    for (const auto &s : _symbols) {
        if (!first) f << ", ";
        f << '"' << s << '"';
        first = false;
    }
    f << "],\n";
    f << "  \"event_types\": [";
    first = true;
    for (const auto &t : _event_types) {
        if (!first) f << ", ";
        f << '"' << t << '"';
        first = false;
    }
    f << "]\n}\n";
}

void ReplayWriter::close() {
    if (_closed) return;
    _closed = true;
    if (_owned_file.is_open()) {
        _owned_file.flush();
        _owned_file.close();
    }
    if (_path) write_companion_json(*_path);
}

} // namespace quarkbot::replay
```

- [ ] **Step 6: Run test — verify it passes**

```bash
cmake --build /tmp/replay_parser_build --target test_reader_writer && \
  /tmp/replay_parser_build/tests/test_reader_writer
```

Expected: `test_writer: PASS`

- [ ] **Step 7: Commit**

```bash
cd /home/ondra/workspace/trading_interface
git add src/backtest/replay_writer.hpp src/backtest/replay_writer.cpp \
        src/backtest/tests/test_helpers.hpp src/backtest/tests/test_reader_writer.cpp
git commit -m "feat(backtest): ReplayWriter with companion JSON"
```

---

## Task 4: ReplayReader + round-trip test

**Files:**
- Create: `src/backtest/replay_reader.hpp`
- Modify: `src/backtest/replay_reader.cpp`
- Modify: `src/backtest/tests/test_reader_writer.cpp`

- [ ] **Step 1: Extend `test_reader_writer.cpp` with failing round-trip tests**

Replace the entire file contents:

```cpp
#include "replay_format.hpp"
#include "replay_reader.hpp"
#include "replay_writer.hpp"
#include "test_helpers.hpp"
#include <chrono>
#include <sstream>
#include <variant>

using namespace quarkbot::replay;
using namespace std::chrono;

static tp_t make_tp(int64_t us) { return tp_t{microseconds{us}}; }

// ── writer unit tests ─────────────────────────────────────────────────────────

static void test_writer_magic() {
    std::ostringstream oss;
    { ReplayWriter w(oss); w.write(TradeEvent{"X", make_tp(1'000'000'000'000'000LL), 1.0, 1.0}); }
    const std::string d = oss.str();
    CHECK(d.size() >= 12);
    CHECK_EQ(std::string(d.data(), 7), "QKRPLAY");
    CHECK_EQ(static_cast<uint8_t>(d[8]), 1u);
}

static void test_writer_record_size() {
    std::ostringstream oss;
    { ReplayWriter w(oss); w.write(TradeEvent{"BTC", make_tp(1'000'000'000'000'000LL), 100.0, 1.0}); }
    // 12 header + 15 RecordHeader + 3 sym + 16 payload (2 doubles)
    CHECK_EQ(oss.str().size(), 12u + 15u + 3u + 16u);
}

// ── reader round-trip tests ───────────────────────────────────────────────────

static void test_roundtrip_trade() {
    const tp_t tp = make_tp(1'704'067'200'000'000LL);
    std::ostringstream oss;
    { ReplayWriter w(oss); w.write(TradeEvent{"BTCUSDT", tp, 42000.5, 0.123}); }
    std::istringstream iss(oss.str());
    ReplayReader r(iss);
    auto ev = r.next();
    CHECK(ev.has_value());
    CHECK(std::holds_alternative<TradeEvent>(*ev));
    const auto &t = std::get<TradeEvent>(*ev);
    CHECK_EQ(t.symbol, "BTCUSDT");
    CHECK_EQ(t.time, tp);
    CHECK_NEAR(t.price, 42000.5, 1e-9);
    CHECK_NEAR(t.size,  0.123,   1e-9);
    CHECK(!r.next().has_value()); // EOF
}

static void test_roundtrip_quote() {
    const tp_t tp = make_tp(1'704'067'200'500'000LL);
    std::ostringstream oss;
    { ReplayWriter w(oss); w.write(QuoteEvent{"ETH", tp, 2500.0, 1.5, 2501.0, 2.0}); }
    std::istringstream iss(oss.str());
    ReplayReader r(iss);
    const auto &q = std::get<QuoteEvent>(*r.next());
    CHECK_EQ(q.symbol, "ETH");
    CHECK_NEAR(q.bid,      2500.0, 1e-9);
    CHECK_NEAR(q.bid_size, 1.5,    1e-9);
    CHECK_NEAR(q.ask,      2501.0, 1e-9);
    CHECK_NEAR(q.ask_size, 2.0,    1e-9);
}

static void test_roundtrip_closed_bar() {
    const tp_t tp = make_tp(1'704'067'260'000'000LL);
    std::ostringstream oss;
    { ReplayWriter w(oss); w.write(ClosedBarEvent{"SOL", tp, 100.0, 105.0, 99.0, 103.0, 500.0, 60}); }
    std::istringstream iss(oss.str());
    const auto &b = std::get<ClosedBarEvent>(*ReplayReader(iss).next());
    CHECK_NEAR(b.open,  100.0, 1e-9);
    CHECK_NEAR(b.high,  105.0, 1e-9);
    CHECK_NEAR(b.low,    99.0, 1e-9);
    CHECK_NEAR(b.close, 103.0, 1e-9);
    CHECK_NEAR(b.volume, 500.0, 1e-9);
    CHECK_EQ(b.interval_sec, 60u);
}

static void test_roundtrip_ob_snapshot() {
    const tp_t tp = make_tp(1'704'067'200'000'000LL);
    const std::vector<OBLevel> levels = {
        {42000.0, 1.0, true},
        {41999.0, 2.0, true},
        {42001.0, 0.5, false},
    };
    std::ostringstream oss;
    {
        ReplayWriter w(oss);
        w.write(OrderBookSnapshotEvent{"BTC", tp, std::span<const OBLevel>(levels)});
    }
    std::istringstream iss(oss.str());
    const auto &s = std::get<OrderBookSnapshotEvent>(*ReplayReader(iss).next());
    CHECK_EQ(s.levels.size(), 3u);
    CHECK_NEAR(s.levels[0].price, 42000.0, 1e-9);
    CHECK(s.levels[0].is_bid);
    CHECK(!s.levels[2].is_bid);
}

static void test_roundtrip_ob_delta() {
    const tp_t tp = make_tp(1'704'067'200'000'000LL);
    std::ostringstream oss;
    { ReplayWriter w(oss); w.write(OrderBookDeltaEvent{"BTC", tp, 42000.0, 0.0, true}); }
    std::istringstream iss(oss.str());
    const auto &d = std::get<OrderBookDeltaEvent>(*ReplayReader(iss).next());
    CHECK_NEAR(d.price, 42000.0, 1e-9);
    CHECK_NEAR(d.size,  0.0,     1e-9);
    CHECK(d.is_bid);
}

static void test_roundtrip_multi_event() {
    const tp_t tp1 = make_tp(1'704'067'200'000'000LL);
    const tp_t tp2 = make_tp(1'704'067'200'100'000LL);
    const tp_t tp3 = make_tp(1'704'067'200'200'000LL);
    std::ostringstream oss;
    {
        ReplayWriter w(oss);
        w.write(TradeEvent{"BTC", tp1, 42000.0, 0.1});
        w.write(QuoteEvent{"ETH", tp2, 2500.0, 1.0, 2501.0, 1.0});
        w.write(TradeEvent{"BTC", tp3, 42010.0, 0.2});
    }
    std::istringstream iss(oss.str());
    ReplayReader r(iss);
    CHECK(std::holds_alternative<TradeEvent>(*r.next()));
    CHECK(std::holds_alternative<QuoteEvent>(*r.next()));
    CHECK(std::holds_alternative<TradeEvent>(*r.next()));
    CHECK(!r.next().has_value());
}

static void test_reset() {
    std::ostringstream oss;
    { ReplayWriter w(oss); w.write(TradeEvent{"X", make_tp(1'000'000'000'000'000LL), 1.0, 1.0}); }
    // Write to a temp file to test reset()
    const std::string path = "/tmp/replay_test_reset.qkrp";
    {
        ReplayWriter w(path);
        w.write(TradeEvent{"X", make_tp(1'000'000'000'000'000LL), 1.0, 1.0});
        w.write(TradeEvent{"Y", make_tp(2'000'000'000'000'000LL), 2.0, 2.0});
    }
    ReplayReader r(path);
    CHECK(r.next().has_value());
    CHECK(r.next().has_value());
    CHECK(!r.next().has_value());
    r.reset();
    CHECK(r.next().has_value()); // back to first event
}

static void test_bad_magic() {
    std::istringstream iss("NOTAFILE");
    bool threw = false;
    try { ReplayReader r(iss); } catch (const std::runtime_error &) { threw = true; }
    CHECK(threw);
}

int main() {
    test_writer_magic();
    test_writer_record_size();
    test_roundtrip_trade();
    test_roundtrip_quote();
    test_roundtrip_closed_bar();
    test_roundtrip_ob_snapshot();
    test_roundtrip_ob_delta();
    test_roundtrip_multi_event();
    test_reset();
    test_bad_magic();
    std::cout << "test_reader_writer: PASS\n";
}
```

- [ ] **Step 2: Run test — verify it fails**

```bash
cmake --build /tmp/replay_parser_build --target test_reader_writer 2>&1 | tail -3
```

Expected: compile error — `replay_reader.hpp` not found.

- [ ] **Step 3: Write `src/backtest/replay_reader.hpp`**

```cpp
#pragma once
#include "replay_format.hpp"
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace quarkbot::replay {

class ReplayReader {
public:
    explicit ReplayReader(std::string_view path);   // file-based; reset() supported
    explicit ReplayReader(std::istream &stream);    // stream-based; reset() throws

    std::optional<Event> next();  // nullopt = EOF; unknown types are skipped
    void reset();                 // seeks back to first record; throws std::logic_error if stream-based

private:
    void read_file_header();

    std::ifstream  _owned_file;
    std::istream  *_in          = nullptr;
    std::streampos _first_record{};
    bool           _file_based  = false;

    std::string          _symbol_buf;
    std::vector<OBLevel> _levels_buf;
};

} // namespace quarkbot::replay
```

- [ ] **Step 4: Write `src/backtest/replay_reader.cpp`**

```cpp
#include "replay_reader.hpp"
#include <cstring>
#include <stdexcept>
#include <vector>

namespace quarkbot::replay {

ReplayReader::ReplayReader(std::string_view path)
    : _owned_file(std::string(path), std::ios::binary)
    , _file_based(true)
{
    if (!_owned_file)
        throw std::runtime_error("Cannot open: " + std::string(path));
    _in = &_owned_file;
    read_file_header();
    _first_record = _in->tellg();
}

ReplayReader::ReplayReader(std::istream &stream)
    : _in(&stream)
    , _file_based(false)
{
    read_file_header();
}

void ReplayReader::read_file_header() {
    FileHeader hdr{};
    _in->read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
    if (!*_in)
        throw std::runtime_error("Cannot read file header");
    if (std::memcmp(hdr.magic, FILE_MAGIC.data(), 8) != 0)
        throw std::runtime_error("Bad magic: not a .qkrp file");
    if (hdr.version != FILE_VERSION)
        throw std::runtime_error("Unsupported version: " + std::to_string(hdr.version));
}

void ReplayReader::reset() {
    if (!_file_based)
        throw std::logic_error("reset() is not supported on stream-based ReplayReader");
    _in->clear();
    _in->seekg(_first_record);
}

static tp_t from_us(int64_t us) {
    return tp_t{std::chrono::microseconds{us}};
}

static void check_read(std::istream &in) {
    if (!in) throw std::runtime_error("Unexpected read error");
}

std::optional<Event> ReplayReader::next() {
    while (true) {
        RecordHeader hdr{};
        _in->read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
        if (_in->eof() && _in->gcount() == 0) return std::nullopt;
        check_read(*_in);

        _symbol_buf.resize(hdr.symbol_len);
        _in->read(_symbol_buf.data(), hdr.symbol_len);
        check_read(*_in);

        const std::string_view sym{_symbol_buf};
        const tp_t             tp = from_us(hdr.timestamp_us);
        const auto             et = static_cast<EventType>(hdr.type);
        const std::size_t      payload_size =
            hdr.record_len - sizeof(RecordHeader) - hdr.symbol_len;

        switch (et) {
            case EventType::trade: {
                TradePayload p{};
                _in->read(reinterpret_cast<char *>(&p), sizeof(p));
                check_read(*_in);
                return TradeEvent{sym, tp, p.price, p.size};
            }
            case EventType::quote: {
                QuotePayload p{};
                _in->read(reinterpret_cast<char *>(&p), sizeof(p));
                check_read(*_in);
                return QuoteEvent{sym, tp, p.bid, p.bid_size, p.ask, p.ask_size};
            }
            case EventType::closed_bar: {
                ClosedBarPayload p{};
                _in->read(reinterpret_cast<char *>(&p), sizeof(p));
                check_read(*_in);
                return ClosedBarEvent{sym, tp, p.open, p.high, p.low, p.close, p.volume, p.interval_sec};
            }
            case EventType::orderbook_snapshot: {
                OBSnapshotHeader sh{};
                _in->read(reinterpret_cast<char *>(&sh), sizeof(sh));
                check_read(*_in);
                _levels_buf.resize(sh.count);
                for (auto &lvl : _levels_buf) {
                    OBLevelWire w{};
                    _in->read(reinterpret_cast<char *>(&w), sizeof(w));
                    check_read(*_in);
                    lvl = {w.price, w.size, w.side == 0};
                }
                return OrderBookSnapshotEvent{sym, tp, std::span<const OBLevel>(_levels_buf)};
            }
            case EventType::orderbook_delta: {
                OBDeltaPayload p{};
                _in->read(reinterpret_cast<char *>(&p), sizeof(p));
                check_read(*_in);
                return OrderBookDeltaEvent{sym, tp, p.price, p.size, p.side == 0};
            }
            default: {
                // Unknown type: skip payload and try next record
                std::vector<char> skip(payload_size);
                _in->read(skip.data(), static_cast<std::streamsize>(payload_size));
                check_read(*_in);
                continue;
            }
        }
    }
}

} // namespace quarkbot::replay
```

- [ ] **Step 5: Run tests — verify all pass**

```bash
cmake --build /tmp/replay_parser_build --target test_reader_writer && \
  /tmp/replay_parser_build/tests/test_reader_writer
```

Expected: `test_reader_writer: PASS`

- [ ] **Step 6: Run full ctest**

```bash
cd /tmp/replay_parser_build && ctest --output-on-failure
```

Expected: test_reader_writer passes (others still have `int main(){}` stubs).

- [ ] **Step 7: Commit**

```bash
cd /home/ondra/workspace/trading_interface
git add src/backtest/replay_reader.hpp src/backtest/replay_reader.cpp \
        src/backtest/tests/test_reader_writer.cpp src/backtest/tests/test_helpers.hpp
git commit -m "feat(backtest): ReplayReader + round-trip tests"
```

---

## Task 5: IConverter interface + CsvOhlcConverter

**Files:**
- Create: `src/backtest/converters/ifc.hpp`
- Create: `src/backtest/converters/csv_parser.hpp`
- Create: `src/backtest/converters/csv_ohlc.hpp`
- Modify: `src/backtest/converters/csv_ohlc.cpp`
- Modify: `src/backtest/tests/test_csv_ohlc.cpp`

- [ ] **Step 1: Write `src/backtest/converters/ifc.hpp`**

```cpp
#pragma once
#include "backtest/replay_writer.hpp"
#include <istream>

namespace quarkbot::replay {

class IConverter {
public:
    virtual void convert(std::istream &in, ReplayWriter &out) = 0;
    virtual ~IConverter() = default;
};

} // namespace quarkbot::replay
```

- [ ] **Step 2: Write `src/backtest/converters/csv_parser.hpp`**

```cpp
#pragma once
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace quarkbot::replay::csv {

// Split a single CSV line into fields, respecting double-quoted strings.
inline std::vector<std::string> split_row(std::string_view line) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    for (char c : line) {
        if (c == '"')       { in_quotes = !in_quotes; }
        else if (c == ',' && !in_quotes) { fields.push_back(std::move(field)); field.clear(); }
        else if (c != '\r') { field += c; }
    }
    fields.push_back(std::move(field));
    return fields;
}

// Map header column names to their index positions.
// Returns index or -1 if not found.
inline int col_index(const std::vector<std::string> &header, std::string_view name) {
    for (int i = 0; i < static_cast<int>(header.size()); ++i)
        if (header[static_cast<std::size_t>(i)] == name) return i;
    return -1;
}

// Parse a timestamp string to microseconds since Unix epoch.
// Accepts:
//   - Integer: treated as ms if > 1e12, otherwise as seconds
//   - ISO 8601: "YYYY-MM-DDTHH:MM:SS[.sss][Z]" or "YYYY-MM-DD HH:MM:SS[.sss]"
inline int64_t parse_timestamp_us(std::string_view s) {
    // Try integer
    char *end = nullptr;
    const long long ival = std::strtoll(s.data(), &end, 10);
    if (end == s.data() + s.size()) {
        // milliseconds if > year 2001 in ms (> 1e12)
        if (ival > 1'000'000'000'000LL) return ival * 1000LL;
        return ival * 1'000'000LL; // seconds
    }
    // Try ISO 8601
    int Y = 0, M = 0, D = 0, h = 0, m = 0;
    double sec = 0.0;
    const char *cs = s.data();
    // Accept both 'T' and ' ' as date-time separator
    int scanned = std::sscanf(cs, "%d-%d-%d %d:%d:%lf", &Y, &M, &D, &h, &m, &sec);
    if (scanned < 6) {
        scanned = std::sscanf(cs, "%d-%d-%dT%d:%d:%lf", &Y, &M, &D, &h, &m, &sec);
    }
    if (scanned < 6) throw std::runtime_error("Cannot parse timestamp: " + std::string(s));

    struct tm t{};
    t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D;
    t.tm_hour = h; t.tm_min = m; t.tm_sec = static_cast<int>(sec);
    const time_t epoch = timegm(&t);
    const int64_t frac_us = static_cast<int64_t>((sec - t.tm_sec) * 1'000'000);
    return static_cast<int64_t>(epoch) * 1'000'000LL + frac_us;
}

} // namespace quarkbot::replay::csv
```

- [ ] **Step 3: Write the failing test in `src/backtest/tests/test_csv_ohlc.cpp`**

```cpp
#include "backtest/converters/csv_ohlc.hpp"
#include "backtest/replay_reader.hpp"
#include "backtest/replay_writer.hpp"
#include "test_helpers.hpp"
#include <sstream>
#include <variant>

using namespace quarkbot::replay;

static void test_basic_ohlc() {
    const std::string csv =
        "time,open,high,low,close,volume\n"
        "1704067200,42000,42500,41800,42300,10.5\n"
        "1704067260,42300,42600,42100,42400,8.2\n";

    std::istringstream in(csv);
    std::ostringstream out;
    {
        ReplayWriter w(out);
        CsvOhlcConverter conv(CsvOhlcConfig{"BTCUSDT", 60});
        conv.convert(in, w);
    }
    std::istringstream result(out.str());
    ReplayReader r(result);

    auto e1 = r.next();
    CHECK(e1.has_value());
    CHECK(std::holds_alternative<ClosedBarEvent>(*e1));
    const auto &b1 = std::get<ClosedBarEvent>(*e1);
    CHECK_EQ(b1.symbol, "BTCUSDT");
    CHECK_NEAR(b1.open,   42000.0, 1e-9);
    CHECK_NEAR(b1.high,   42500.0, 1e-9);
    CHECK_NEAR(b1.low,    41800.0, 1e-9);
    CHECK_NEAR(b1.close,  42300.0, 1e-9);
    CHECK_NEAR(b1.volume, 10.5,    1e-9);
    CHECK_EQ(b1.interval_sec, 60u);

    auto e2 = r.next();
    CHECK(e2.has_value());
    const auto &b2 = std::get<ClosedBarEvent>(*e2);
    CHECK_NEAR(b2.open, 42300.0, 1e-9);
    CHECK(!r.next().has_value());
}

static void test_iso_timestamp() {
    const std::string csv =
        "time,open,high,low,close,volume\n"
        "2024-01-01T00:00:00Z,100,110,90,105,50\n";
    std::istringstream in(csv);
    std::ostringstream out;
    {
        ReplayWriter w(out);
        CsvOhlcConverter conv(CsvOhlcConfig{"SOL", 3600});
        conv.convert(in, w);
    }
    std::istringstream result(out.str());
    const auto &b = std::get<ClosedBarEvent>(*ReplayReader(result).next());
    CHECK_NEAR(b.open, 100.0, 1e-9);
    CHECK_EQ(b.interval_sec, 3600u);
}

static void test_custom_column_names() {
    const std::string csv =
        "ts,o,h,l,c,vol\n"
        "1704067200,1.0,2.0,0.5,1.5,100\n";
    std::istringstream in(csv);
    std::ostringstream out;
    {
        ReplayWriter w(out);
        CsvOhlcConfig cfg{"ETH", 60};
        cfg.col_time = "ts"; cfg.col_open = "o"; cfg.col_high = "h";
        cfg.col_low  = "l";  cfg.col_close = "c"; cfg.col_volume = "vol";
        CsvOhlcConverter conv(cfg);
        conv.convert(in, w);
    }
    std::istringstream result(out.str());
    const auto &b = std::get<ClosedBarEvent>(*ReplayReader(result).next());
    CHECK_NEAR(b.open, 1.0, 1e-9);
    CHECK_NEAR(b.close, 1.5, 1e-9);
}

int main() {
    test_basic_ohlc();
    test_iso_timestamp();
    test_custom_column_names();
    std::cout << "test_csv_ohlc: PASS\n";
}
```

- [ ] **Step 4: Run — verify compile error**

```bash
cmake --build /tmp/replay_parser_build --target test_csv_ohlc 2>&1 | tail -3
```

Expected: `csv_ohlc.hpp` not found.

- [ ] **Step 5: Write `src/backtest/converters/csv_ohlc.hpp`**

```cpp
#pragma once
#include "ifc.hpp"
#include <string>

namespace quarkbot::replay {

struct CsvOhlcConfig {
    std::string symbol;            // symbol to assign (not in file)
    uint32_t    interval_sec = 60; // bar interval
    std::string col_time   = "time";
    std::string col_open   = "open";
    std::string col_high   = "high";
    std::string col_low    = "low";
    std::string col_close  = "close";
    std::string col_volume = "volume";
};

class CsvOhlcConverter : public IConverter {
public:
    explicit CsvOhlcConverter(CsvOhlcConfig cfg) : _cfg(std::move(cfg)) {}
    void convert(std::istream &in, ReplayWriter &out) override;
private:
    CsvOhlcConfig _cfg;
};

} // namespace quarkbot::replay
```

- [ ] **Step 6: Write `src/backtest/converters/csv_ohlc.cpp`**

```cpp
#include "csv_ohlc.hpp"
#include "csv_parser.hpp"
#include <chrono>
#include <stdexcept>
#include <string>

namespace quarkbot::replay {

void CsvOhlcConverter::convert(std::istream &in, ReplayWriter &out) {
    std::string line;
    // Read header
    if (!std::getline(in, line))
        throw std::runtime_error("CsvOhlcConverter: empty input");
    const auto header = csv::split_row(line);

    const int ci_time   = csv::col_index(header, _cfg.col_time);
    const int ci_open   = csv::col_index(header, _cfg.col_open);
    const int ci_high   = csv::col_index(header, _cfg.col_high);
    const int ci_low    = csv::col_index(header, _cfg.col_low);
    const int ci_close  = csv::col_index(header, _cfg.col_close);
    const int ci_volume = csv::col_index(header, _cfg.col_volume);

    if (ci_time < 0)  throw std::runtime_error("CsvOhlcConverter: missing column: " + _cfg.col_time);
    if (ci_open < 0)  throw std::runtime_error("CsvOhlcConverter: missing column: " + _cfg.col_open);
    if (ci_high < 0)  throw std::runtime_error("CsvOhlcConverter: missing column: " + _cfg.col_high);
    if (ci_low < 0)   throw std::runtime_error("CsvOhlcConverter: missing column: " + _cfg.col_low);
    if (ci_close < 0) throw std::runtime_error("CsvOhlcConverter: missing column: " + _cfg.col_close);

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = csv::split_row(line);

        const int64_t ts_us = csv::parse_timestamp_us(fields[static_cast<std::size_t>(ci_time)]);
        const tp_t    tp{std::chrono::microseconds{ts_us}};

        const double open   = std::stod(fields[static_cast<std::size_t>(ci_open)]);
        const double high   = std::stod(fields[static_cast<std::size_t>(ci_high)]);
        const double low    = std::stod(fields[static_cast<std::size_t>(ci_low)]);
        const double close  = std::stod(fields[static_cast<std::size_t>(ci_close)]);
        const double volume = (ci_volume >= 0)
            ? std::stod(fields[static_cast<std::size_t>(ci_volume)])
            : 0.0;

        out.write(ClosedBarEvent{_cfg.symbol, tp, open, high, low, close, volume, _cfg.interval_sec});
    }
}

} // namespace quarkbot::replay
```

- [ ] **Step 7: Run tests — verify pass**

```bash
cmake --build /tmp/replay_parser_build --target test_csv_ohlc && \
  /tmp/replay_parser_build/tests/test_csv_ohlc
```

Expected: `test_csv_ohlc: PASS`

- [ ] **Step 8: Commit**

```bash
cd /home/ondra/workspace/trading_interface
git add src/backtest/converters/
git commit -m "feat(backtest): IConverter, csv_parser, CsvOhlcConverter"
```

---

## Task 6: CsvTickConverter

**Files:**
- Create: `src/backtest/converters/csv_tick.hpp`
- Modify: `src/backtest/converters/csv_tick.cpp`
- Modify: `src/backtest/tests/test_csv_tick.cpp`

- [ ] **Step 1: Write the failing test in `src/backtest/tests/test_csv_tick.cpp`**

```cpp
#include "backtest/converters/csv_tick.hpp"
#include "backtest/replay_reader.hpp"
#include "backtest/replay_writer.hpp"
#include "test_helpers.hpp"
#include <sstream>
#include <variant>

using namespace quarkbot::replay;

static void test_trade_only() {
    const std::string csv =
        "time,price,size\n"
        "1704067200,42000.0,0.1\n"
        "1704067201,42001.0,0.2\n";
    std::istringstream in(csv);
    std::ostringstream out;
    {
        ReplayWriter w(out);
        CsvTickConverter conv(CsvTickConfig{"BTCUSDT"});
        conv.convert(in, w);
    }
    std::istringstream result(out.str());
    ReplayReader r(result);
    const auto &t1 = std::get<TradeEvent>(*r.next());
    CHECK_EQ(t1.symbol, "BTCUSDT");
    CHECK_NEAR(t1.price, 42000.0, 1e-9);
    CHECK_NEAR(t1.size,  0.1,     1e-9);
    const auto &t2 = std::get<TradeEvent>(*r.next());
    CHECK_NEAR(t2.price, 42001.0, 1e-9);
    CHECK(!r.next().has_value());
}

static void test_trade_and_quote() {
    // When bid/ask columns present, emit both Trade and Quote per row
    const std::string csv =
        "time,price,size,bid,bid_size,ask,ask_size\n"
        "1704067200,42000,0.1,41999,1.5,42001,2.0\n";
    std::istringstream in(csv);
    std::ostringstream out;
    {
        ReplayWriter w(out);
        CsvTickConverter conv(CsvTickConfig{"BTC"});
        conv.convert(in, w);
    }
    std::istringstream result(out.str());
    ReplayReader r(result);
    // Events emitted: Trade first, then Quote (same timestamp)
    CHECK(std::holds_alternative<TradeEvent>(*r.next()));
    const auto &q = std::get<QuoteEvent>(*r.next());
    CHECK_NEAR(q.bid,      41999.0, 1e-9);
    CHECK_NEAR(q.bid_size, 1.5,     1e-9);
    CHECK_NEAR(q.ask,      42001.0, 1e-9);
    CHECK_NEAR(q.ask_size, 2.0,     1e-9);
    CHECK(!r.next().has_value());
}

static void test_symbol_column() {
    const std::string csv =
        "time,symbol,price,size\n"
        "1704067200,BTC,42000,0.1\n"
        "1704067201,ETH,2500,1.0\n";
    std::istringstream in(csv);
    std::ostringstream out;
    {
        ReplayWriter w(out);
        CsvTickConverter conv(CsvTickConfig{""});
        conv.convert(in, w);
    }
    std::istringstream result(out.str());
    ReplayReader r(result);
    CHECK_EQ(std::get<TradeEvent>(*r.next()).symbol, "BTC");
    CHECK_EQ(std::get<TradeEvent>(*r.next()).symbol, "ETH");
}

static void test_mmbot_format() {
    // Legacy mmbot CSV: timestamp,symbol,bid,ask,bid_size,ask_size,trade,volume,index
    const std::string csv =
        "timestamp,symbol,bid,ask,bid_size,ask_size,trade,volume,index\n"
        "1704067200,BTCUSDT,41999,42001,1.5,2.0,42000,0.5,42000\n"
        "1704067201,BTCUSDT,42000,42002,1.0,1.0,,0,42001\n"; // empty trade = no TradeEvent
    std::istringstream in(csv);
    std::ostringstream out;
    {
        ReplayWriter w(out);
        CsvTickConfig cfg{""};
        cfg.col_time     = "timestamp";
        cfg.col_bid      = "bid";    cfg.col_ask      = "ask";
        cfg.col_bid_size = "bid_size"; cfg.col_ask_size = "ask_size";
        cfg.col_price    = "trade";  cfg.col_size     = "volume";
        CsvTickConverter conv(cfg);
        conv.convert(in, w);
    }
    std::istringstream result(out.str());
    ReplayReader r(result);
    // Row 1: Trade + Quote
    CHECK(std::holds_alternative<TradeEvent>(*r.next()));
    CHECK(std::holds_alternative<QuoteEvent>(*r.next()));
    // Row 2: Quote only (trade field empty)
    CHECK(std::holds_alternative<QuoteEvent>(*r.next()));
    CHECK(!r.next().has_value());
}

int main() {
    test_trade_only();
    test_trade_and_quote();
    test_symbol_column();
    test_mmbot_format();
    std::cout << "test_csv_tick: PASS\n";
}
```

- [ ] **Step 2: Run — verify compile error**

```bash
cmake --build /tmp/replay_parser_build --target test_csv_tick 2>&1 | tail -3
```

Expected: `csv_tick.hpp` not found.

- [ ] **Step 3: Write `src/backtest/converters/csv_tick.hpp`**

```cpp
#pragma once
#include "ifc.hpp"
#include <string>

namespace quarkbot::replay {

struct CsvTickConfig {
    std::string default_symbol;          // used when no symbol column present
    std::string col_time     = "time";
    std::string col_symbol   = "symbol"; // empty = always use default_symbol
    std::string col_price    = "price";
    std::string col_size     = "size";
    std::string col_bid      = "bid";
    std::string col_ask      = "ask";
    std::string col_bid_size = "bid_size";
    std::string col_ask_size = "ask_size";
};

class CsvTickConverter : public IConverter {
public:
    explicit CsvTickConverter(CsvTickConfig cfg) : _cfg(std::move(cfg)) {}
    void convert(std::istream &in, ReplayWriter &out) override;
private:
    CsvTickConfig _cfg;
};

} // namespace quarkbot::replay
```

- [ ] **Step 4: Write `src/backtest/converters/csv_tick.cpp`**

```cpp
#include "csv_tick.hpp"
#include "csv_parser.hpp"
#include <chrono>
#include <stdexcept>

namespace quarkbot::replay {

void CsvTickConverter::convert(std::istream &in, ReplayWriter &out) {
    std::string line;
    if (!std::getline(in, line))
        throw std::runtime_error("CsvTickConverter: empty input");
    const auto header = csv::split_row(line);

    const int ci_time     = csv::col_index(header, _cfg.col_time);
    const int ci_symbol   = _cfg.col_symbol.empty() ? -1 : csv::col_index(header, _cfg.col_symbol);
    const int ci_price    = csv::col_index(header, _cfg.col_price);
    const int ci_size     = csv::col_index(header, _cfg.col_size);
    const int ci_bid      = csv::col_index(header, _cfg.col_bid);
    const int ci_ask      = csv::col_index(header, _cfg.col_ask);
    const int ci_bid_size = csv::col_index(header, _cfg.col_bid_size);
    const int ci_ask_size = csv::col_index(header, _cfg.col_ask_size);

    if (ci_time < 0)
        throw std::runtime_error("CsvTickConverter: missing column: " + _cfg.col_time);

    const bool has_quote = (ci_bid >= 0 && ci_ask >= 0 &&
                            ci_bid_size >= 0 && ci_ask_size >= 0);

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = csv::split_row(line);

        const int64_t ts_us = csv::parse_timestamp_us(fields[static_cast<std::size_t>(ci_time)]);
        const tp_t    tp{std::chrono::microseconds{ts_us}};

        const std::string sym = (ci_symbol >= 0)
            ? fields[static_cast<std::size_t>(ci_symbol)]
            : _cfg.default_symbol;

        // Emit TradeEvent only if price column is present and non-empty
        if (ci_price >= 0 && ci_size >= 0) {
            const std::string &price_str = fields[static_cast<std::size_t>(ci_price)];
            const std::string &size_str  = fields[static_cast<std::size_t>(ci_size)];
            if (!price_str.empty() && !size_str.empty()) {
                const double price = std::stod(price_str);
                const double size  = std::stod(size_str);
                if (size > 0.0)
                    out.write(TradeEvent{sym, tp, price, size});
            }
        }

        // Emit QuoteEvent if all bid/ask columns are present and non-empty
        if (has_quote) {
            const std::string &bid_s  = fields[static_cast<std::size_t>(ci_bid)];
            const std::string &ask_s  = fields[static_cast<std::size_t>(ci_ask)];
            const std::string &bsz_s  = fields[static_cast<std::size_t>(ci_bid_size)];
            const std::string &asz_s  = fields[static_cast<std::size_t>(ci_ask_size)];
            if (!bid_s.empty() && !ask_s.empty() && !bsz_s.empty() && !asz_s.empty()) {
                out.write(QuoteEvent{sym, tp,
                    std::stod(bid_s), std::stod(bsz_s),
                    std::stod(ask_s), std::stod(asz_s)});
            }
        }
    }
}

} // namespace quarkbot::replay
```

- [ ] **Step 5: Run tests — verify pass**

```bash
cmake --build /tmp/replay_parser_build --target test_csv_tick && \
  /tmp/replay_parser_build/tests/test_csv_tick
```

Expected: `test_csv_tick: PASS`

- [ ] **Step 6: Commit**

```bash
cd /home/ondra/workspace/trading_interface
git add src/backtest/converters/csv_tick.hpp src/backtest/converters/csv_tick.cpp \
        src/backtest/tests/test_csv_tick.cpp
git commit -m "feat(backtest): CsvTickConverter with mmbot format support"
```

---

## Task 7: JsonExchangeConverter

**Files:**
- Create: `src/backtest/converters/json_exchange.hpp`
- Modify: `src/backtest/converters/json_exchange.cpp`
- Modify: `src/backtest/tests/test_json_exchange.cpp`

- [ ] **Step 1: Write failing test in `src/backtest/tests/test_json_exchange.cpp`**

```cpp
#include "backtest/converters/json_exchange.hpp"
#include "backtest/replay_reader.hpp"
#include "backtest/replay_writer.hpp"
#include "test_helpers.hpp"
#include <sstream>
#include <variant>

using namespace quarkbot::replay;

static void test_ndjson_trades() {
    // Newline-delimited JSON, Binance aggTrade-style
    const std::string ndjson =
        R"({"type":"aggTrade","symbol":"BTCUSDT","time":1704067200000,"price":"42000.5","qty":"0.1"})" "\n"
        R"({"type":"aggTrade","symbol":"BTCUSDT","time":1704067200100,"price":"42001.0","qty":"0.2"})" "\n";

    JsonFieldMap fm;
    fm.field_time   = "time";
    fm.field_symbol = "symbol";
    fm.field_price  = "price";
    fm.field_size   = "qty";
    fm.type_map     = {{"aggTrade", "trade"}};

    std::istringstream in(ndjson);
    std::ostringstream out;
    {
        ReplayWriter w(out);
        JsonExchangeConverter conv(fm);
        conv.convert(in, w);
    }
    std::istringstream result(out.str());
    ReplayReader r(result);
    const auto &t1 = std::get<TradeEvent>(*r.next());
    CHECK_EQ(t1.symbol, "BTCUSDT");
    CHECK_NEAR(t1.price, 42000.5, 1e-9);
    CHECK_NEAR(t1.size,  0.1,     1e-9);
    CHECK(std::holds_alternative<TradeEvent>(*r.next()));
    CHECK(!r.next().has_value());
}

static void test_json_array_quotes() {
    const std::string json =
        R"([)" "\n"
        R"(  {"type":"quote","sym":"ETHUSDT","ts":1704067200000,"b":"2500.0","bs":"1.5","a":"2501.0","as":"2.0"},)" "\n"
        R"(  {"type":"quote","sym":"ETHUSDT","ts":1704067200500,"b":"2500.5","bs":"1.0","a":"2501.5","as":"1.0"})" "\n"
        R"(])" "\n";

    JsonFieldMap fm;
    fm.field_type     = "type";
    fm.field_symbol   = "sym";
    fm.field_time     = "ts";
    fm.field_bid      = "b";
    fm.field_bid_size = "bs";
    fm.field_ask      = "a";
    fm.field_ask_size = "as";
    fm.type_map       = {{"quote", "quote"}};

    std::istringstream in(json);
    std::ostringstream out;
    {
        ReplayWriter w(out);
        JsonExchangeConverter conv(fm);
        conv.convert(in, w);
    }
    std::istringstream result(out.str());
    ReplayReader r(result);
    const auto &q = std::get<QuoteEvent>(*r.next());
    CHECK_EQ(q.symbol, "ETHUSDT");
    CHECK_NEAR(q.bid,      2500.0, 1e-9);
    CHECK_NEAR(q.bid_size, 1.5,    1e-9);
    CHECK(std::holds_alternative<QuoteEvent>(*r.next()));
    CHECK(!r.next().has_value());
}

static void test_unknown_type_skipped() {
    // Events with unmapped type are silently skipped
    const std::string ndjson =
        R"({"type":"unknown","symbol":"X","time":1704067200000,"price":"1","qty":"1"})" "\n"
        R"({"type":"aggTrade","symbol":"X","time":1704067200100,"price":"2","qty":"1"})" "\n";

    JsonFieldMap fm;
    fm.field_price = "price";
    fm.field_size  = "qty";
    fm.type_map    = {{"aggTrade", "trade"}};

    std::istringstream in(ndjson);
    std::ostringstream out;
    { ReplayWriter w(out); JsonExchangeConverter conv(fm); conv.convert(in, w); }
    std::istringstream result(out.str());
    ReplayReader r(result);
    CHECK(std::holds_alternative<TradeEvent>(*r.next())); // only one event
    CHECK(!r.next().has_value());
}

int main() {
    test_ndjson_trades();
    test_json_array_quotes();
    test_unknown_type_skipped();
    std::cout << "test_json_exchange: PASS\n";
}
```

- [ ] **Step 2: Run — verify compile error**

```bash
cmake --build /tmp/replay_parser_build --target test_json_exchange 2>&1 | tail -3
```

Expected: `json_exchange.hpp` not found.

- [ ] **Step 3: Write `src/backtest/converters/json_exchange.hpp`**

```cpp
#pragma once
#include "ifc.hpp"
#include <map>
#include <string>

namespace quarkbot::replay {

struct JsonFieldMap {
    std::string field_type     = "type";
    std::string field_symbol   = "symbol";
    std::string field_time     = "time";       // ms epoch integer or ISO 8601 string
    std::string field_price    = "price";      // for trade
    std::string field_size     = "size";       // for trade
    std::string field_bid      = "bid";        // for quote
    std::string field_ask      = "ask";        // for quote
    std::string field_bid_size = "bid_size";   // for quote
    std::string field_ask_size = "ask_size";   // for quote
    std::string field_levels   = "levels";     // array field for orderbook snapshot
    std::string field_ob_price = "price";      // within each level object
    std::string field_ob_size  = "size";       // within each level object
    std::string field_ob_side  = "side";       // within each level object ("bid"/"ask")
    // Maps source type strings to canonical names: "trade","quote","closed_bar",
    // "orderbook_snapshot","orderbook_delta"
    std::map<std::string, std::string> type_map;
};

class JsonExchangeConverter : public IConverter {
public:
    explicit JsonExchangeConverter(JsonFieldMap fm) : _fm(std::move(fm)) {}
    void convert(std::istream &in, ReplayWriter &out) override;
private:
    JsonFieldMap _fm;
};

} // namespace quarkbot::replay
```

- [ ] **Step 4: Write `src/backtest/converters/json_exchange.cpp`**

```cpp
#include "json_exchange.hpp"
#include "csv_parser.hpp"   // for parse_timestamp_us
#include <nlohmann/json.hpp>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace quarkbot::replay {

// Extract a double value from a JSON field that may be a number or a string.
static double jdouble(const json &j, const std::string &key) {
    const auto &v = j.at(key);
    if (v.is_number()) return v.get<double>();
    return std::stod(v.get<std::string>());
}

// Extract timestamp in microseconds from a JSON field.
static int64_t jtime_us(const json &j, const std::string &key) {
    const auto &v = j.at(key);
    if (v.is_number_integer()) {
        const int64_t val = v.get<int64_t>();
        return (val > 1'000'000'000'000LL) ? val * 1000LL : val * 1'000'000LL;
    }
    return csv::parse_timestamp_us(v.get<std::string>());
}

static void process_object(const json &obj, const JsonFieldMap &fm, ReplayWriter &out) {
    // Determine canonical event type
    if (!obj.contains(fm.field_type)) return;
    const std::string src_type = obj.at(fm.field_type).get<std::string>();
    const auto it = fm.type_map.find(src_type);
    if (it == fm.type_map.end()) return; // unknown type — skip
    const std::string &canonical = it->second;

    const std::string sym = obj.contains(fm.field_symbol)
        ? obj.at(fm.field_symbol).get<std::string>() : "";
    const int64_t ts_us   = jtime_us(obj, fm.field_time);
    const tp_t    tp{std::chrono::microseconds{ts_us}};

    if (canonical == "trade") {
        out.write(TradeEvent{sym, tp,
            jdouble(obj, fm.field_price),
            jdouble(obj, fm.field_size)});

    } else if (canonical == "quote") {
        out.write(QuoteEvent{sym, tp,
            jdouble(obj, fm.field_bid),
            jdouble(obj, fm.field_bid_size),
            jdouble(obj, fm.field_ask),
            jdouble(obj, fm.field_ask_size)});

    } else if (canonical == "closed_bar") {
        out.write(ClosedBarEvent{sym, tp,
            jdouble(obj, "open"), jdouble(obj, "high"),
            jdouble(obj, "low"),  jdouble(obj, "close"),
            obj.contains("volume") ? jdouble(obj, "volume") : 0.0,
            obj.contains("interval_sec") ? obj.at("interval_sec").get<uint32_t>() : 60u});

    } else if (canonical == "orderbook_snapshot") {
        const auto &arr = obj.at(fm.field_levels);
        std::vector<OBLevel> levels;
        levels.reserve(arr.size());
        for (const auto &lvl : arr) {
            const std::string side = lvl.at(fm.field_ob_side).get<std::string>();
            levels.push_back({jdouble(lvl, fm.field_ob_price),
                              jdouble(lvl, fm.field_ob_size),
                              side == "bid"});
        }
        out.write(OrderBookSnapshotEvent{sym, tp, std::span<const OBLevel>(levels)});

    } else if (canonical == "orderbook_delta") {
        const std::string side = obj.at(fm.field_ob_side).get<std::string>();
        out.write(OrderBookDeltaEvent{sym, tp,
            jdouble(obj, fm.field_ob_price),
            jdouble(obj, fm.field_ob_size),
            side == "bid"});
    }
}

void JsonExchangeConverter::convert(std::istream &in, ReplayWriter &out) {
    // Peek at first non-whitespace character to detect format
    char first = '\0';
    while (in.get(first) && std::isspace(static_cast<unsigned char>(first))) {}
    if (!in) return;
    in.putback(first);

    if (first == '[') {
        // JSON array: parse whole document
        json doc;
        in >> doc;
        for (const auto &obj : doc)
            process_object(obj, _fm, out);
    } else {
        // NDJSON: one JSON object per line
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            try {
                process_object(json::parse(line), _fm, out);
            } catch (const json::exception &) {
                continue; // skip malformed lines
            }
        }
    }
}

} // namespace quarkbot::replay
```

- [ ] **Step 5: Run tests — verify pass**

```bash
cmake --build /tmp/replay_parser_build --target test_json_exchange && \
  /tmp/replay_parser_build/tests/test_json_exchange
```

Expected: `test_json_exchange: PASS`

- [ ] **Step 6: Run full ctest**

```bash
cd /tmp/replay_parser_build && ctest --output-on-failure
```

Expected: all 4 tests pass.

- [ ] **Step 7: Commit**

```bash
cd /home/ondra/workspace/trading_interface
git add src/backtest/converters/json_exchange.hpp \
        src/backtest/converters/json_exchange.cpp \
        src/backtest/tests/test_json_exchange.cpp
git commit -m "feat(backtest): JsonExchangeConverter using nlohmann/json"
```

---

## Task 8: Wire up to parent CMake

**Files:**
- Modify: `CMakeLists.txt` (root)

- [ ] **Step 1: Add `add_subdirectory` to root `CMakeLists.txt`**

Open `/home/ondra/workspace/trading_interface/CMakeLists.txt` and add after the existing `add_subdirectory` lines:

```cmake
add_subdirectory("src/backtest")
```

- [ ] **Step 2: Verify parent build includes the library**

```bash
cmake -S /home/ondra/workspace/trading_interface \
      -B /tmp/trading_full_build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/trading_full_build --target replay_parser
```

Expected: `replay_parser` static library built successfully.

- [ ] **Step 3: Run backtest tests via parent ctest**

```bash
cd /tmp/trading_full_build && ctest -R "test_reader_writer|test_csv" --output-on-failure
```

Expected: all replay_parser tests pass.

- [ ] **Step 4: Commit**

```bash
cd /home/ondra/workspace/trading_interface
git add CMakeLists.txt
git commit -m "feat: wire replay_parser into parent CMake build"
```
