# Auction Trades as Qualified `Trade` Events Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Carry auction results as `Trade` events with a `TradeQualifier`, reduce `Auction` to indicative data, and make `SimExecutor` simulate auctions realistically (call phase, single-price uncross for auction and regular orders, stop triggering).

**Architecture:** A new pure state machine `AuctionPhaseTracker` (per instrument, keyed by name) receives indicative auctions, trades and the closing timeout and returns `Actions` (uncross / finish / close_day / start timeout). `SimExecutor` delegates every phase decision to it and only executes the actions on its order list. Data sources (Algoseek, TRTH, replay CSV) emit auction prints as `Trade` with a qualifier.

**Tech Stack:** C++23, CMake, BasicCoro coroutines, custom `CHECK` macros (`src/tests/check.h`), no external test framework.

**Spec:** `docs/superpowers/specs/2026-10-15-auction-trade-qualifier-design.md`

## Global Constraints

- Build with the existing `build/` directory (configured with `/usr/bin/clang++`). The default `/usr/bin/c++` cannot build this repo. When configuring a fresh directory, pass `-DCMAKE_CXX_COMPILER=/usr/bin/clang++`.
- Build a single test: `cmake --build build -j$(nproc) --target test_<name>`. Run it: `./build/tests/test_<name>`. Full suite: `ctest --test-dir build`.
- A new test `.cpp` must be added to `BASIC_TESTS` in `src/tests/CMakeLists.txt`. The binary is then `test_<filename-without-.cpp>`.
- Financial values use `Decimal`, never `double`.
- `TradeQualifier` values: `continuous = 0`, `opening_auction = 'O'`, `closing_auction = 'C'`, `intraday_auction = 'I'`, `unscheduled_auction = 'U'`.
- `Trade::qualifier` is the **last** member and defaults to `TradeQualifier::continuous`.
- Intraday and unscheduled duplicate window: 2 minutes. Closing timeout: 30 minutes (unchanged).
- Auction fills are charged as taker (unchanged).
- Match the style of the surrounding code: `///` doc comments, `//` explanatory comments that say *why*, 4-space indentation.
- Every commit message ends with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```

## Review Focus

- An order with an auction time in force (`ato` / `atc` / `crossing`) must never be filled by continuous trades or quotes. Today `match_order(order, Trade)` does not check the time in force. Pinned in Task 3 (`test_opening_without_indicative`: `atc_lim` survives a crossing continuous trade).
- A duplicate closing print after the close must not reopen the day, and nothing may match while closed. Pinned in Task 2 (`test_closing_with_indicative`) and Task 3 (`test_closing_auction`: `gtc_far` stays unfilled under a crossing quote).
- Instruments that never see an auction (crypto) must behave exactly as before. Pinned in Task 3 (`test_no_auctions`) and the full `ctest` run in Task 6.
- Algoseek fake quotes must be generated for continuous trades only, never for auction prints. Pinned in Task 4 (`test_fake_quotes_skip_auctions`).
- Old replay CSV files using the removed `F` flag must fail loudly, not load with a silently different meaning. Pinned in Task 5 (`test_final_flag_rejected`).

---

### Task 1: `TradeQualifier` and the `Trade::qualifier` field

**Files:**
- Modify: `include/quarkbot/stream/trade.hpp`
- Create: `src/tests/trade_qualifier.cpp`
- Modify: `src/tests/CMakeLists.txt` (`BASIC_TESTS`)

**Interfaces:**
- Consumes: `AuctionType` from `include/quarkbot/stream/auction.hpp`.
- Produces:
  - `enum class TradeQualifier : char { continuous = 0, opening_auction = 'O', closing_auction = 'C', intraday_auction = 'I', unscheduled_auction = 'U' };`
  - `Trade::qualifier` (type `TradeQualifier`, default `continuous`, last member)
  - `constexpr bool is_auction_trade(TradeQualifier q);`
  - `constexpr AuctionType to_auction_type(TradeQualifier q);` (`continuous` → `AuctionType::unknown`)
  - `constexpr TradeQualifier to_trade_qualifier(AuctionType t);` (`unknown` → `continuous`)

- [ ] **Step 1: Write the failing test**

Create `src/tests/trade_qualifier.cpp`:

```cpp
#include "check.h"
#include "quarkbot/stream/trade.hpp"
#include <array>
#include <utility>

using namespace quarkbot;

static_assert(static_cast<char>(TradeQualifier::opening_auction) == static_cast<char>(AuctionType::opening));
static_assert(static_cast<char>(TradeQualifier::closing_auction) == static_cast<char>(AuctionType::closing));
static_assert(static_cast<char>(TradeQualifier::intraday_auction) == static_cast<char>(AuctionType::intraday));
static_assert(static_cast<char>(TradeQualifier::unscheduled_auction) == static_cast<char>(AuctionType::unscheduled));

int main() {
    //existing four-member aggregate initialization still compiles and
    //produces a continuous trade
    Trade t{100_dec, 1_dec, {}, Side::buy};
    CHECK(t.qualifier == TradeQualifier::continuous);
    CHECK(!is_auction_trade(t.qualifier));

    constexpr std::array<std::pair<TradeQualifier, AuctionType>, 4> pairs{{
        {TradeQualifier::opening_auction, AuctionType::opening},
        {TradeQualifier::closing_auction, AuctionType::closing},
        {TradeQualifier::intraday_auction, AuctionType::intraday},
        {TradeQualifier::unscheduled_auction, AuctionType::unscheduled},
    }};
    for (auto [q, a]: pairs) {
        CHECK(is_auction_trade(q));
        CHECK(to_auction_type(q) == a);
        CHECK(to_trade_qualifier(a) == q);
    }
    CHECK(to_auction_type(TradeQualifier::continuous) == AuctionType::unknown);
    CHECK(to_trade_qualifier(AuctionType::unknown) == TradeQualifier::continuous);

    //the qualifier takes part in equality
    Trade a = t;
    a.qualifier = TradeQualifier::closing_auction;
    CHECK(!(a == t));
    return 0;
}
```

In `src/tests/CMakeLists.txt`, add `trade_qualifier.cpp` to `BASIC_TESTS` right after `replay_csv.cpp`:

```cmake
    replay_csv.cpp
    trade_qualifier.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_trade_qualifier`
Expected: the compile fails with `no member named 'qualifier'` / `use of undeclared identifier 'TradeQualifier'`.

- [ ] **Step 3: Implement**

Replace the whole content of `include/quarkbot/stream/trade.hpp` with:

```cpp
#pragma once

#include "../types.hpp"
#include "auction.hpp"

namespace quarkbot {

///Trading phase a trade was executed in
/**
 * Follows the MMT level 2 "trading mode" (FIX TradingSessionSubID carries the
 * same idea). The auction values use the same characters as AuctionType.
 */
enum class TradeQualifier : char {
    ///continuous trading phase
    continuous = 0,
    opening_auction = 'O',
    closing_auction = 'C',
    intraday_auction = 'I',
    ///unscheduled auction, e.g. reopening after a halt
    unscheduled_auction = 'U',
};

struct Trade{
    struct MarketInstrumentStream {};
    ///trade price
    Decimal price;
    ///trade volume
    Decimal size;
    ///time of execution
    std::chrono::system_clock::time_point time;
    ///taker's side - this is optional - exchange don't need to report side
    Side side  = Side::undetermined;
    ///phase the trade was executed in - an auction result is a trade with an auction qualifier
    TradeQualifier qualifier = TradeQualifier::continuous;

    bool operator==(const Trade &) const  = default;
};

///true when the trade is an auction result
constexpr bool is_auction_trade(TradeQualifier q) {
    return q != TradeQualifier::continuous;
}

///auction type of an auction trade, AuctionType::unknown for a continuous trade
constexpr AuctionType to_auction_type(TradeQualifier q) {
    switch (q) {
        case TradeQualifier::opening_auction: return AuctionType::opening;
        case TradeQualifier::closing_auction: return AuctionType::closing;
        case TradeQualifier::intraday_auction: return AuctionType::intraday;
        case TradeQualifier::unscheduled_auction: return AuctionType::unscheduled;
        default: return AuctionType::unknown;
    }
}

///qualifier of a trade resulting from the given auction, continuous for AuctionType::unknown
constexpr TradeQualifier to_trade_qualifier(AuctionType t) {
    switch (t) {
        case AuctionType::opening: return TradeQualifier::opening_auction;
        case AuctionType::closing: return TradeQualifier::closing_auction;
        case AuctionType::intraday: return TradeQualifier::intraday_auction;
        case AuctionType::unscheduled: return TradeQualifier::unscheduled_auction;
        default: return TradeQualifier::continuous;
    }
}

}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build -j$(nproc) --target test_trade_qualifier && ./build/tests/test_trade_qualifier`
Expected: only `Passed:` lines, exit code 0.

Run: `cmake --build build -j$(nproc)`
Expected: the whole tree still builds (the new member is last and defaulted).

- [ ] **Step 5: Commit**

```bash
git add include/quarkbot/stream/trade.hpp src/tests/trade_qualifier.cpp src/tests/CMakeLists.txt
git commit -m "stream: add TradeQualifier to Trade

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `AuctionPhaseTracker` state machine

**Files:**
- Create: `src/quarkbot/backtest/auction_phase_tracker.hpp`
- Create: `src/quarkbot/backtest/auction_phase_tracker.cpp`
- Modify: `src/quarkbot/backtest/CMakeLists.txt` (`target_sources`)
- Create: `src/tests/auction_phase_tracker_test.cpp`
- Modify: `src/tests/CMakeLists.txt` (`BASIC_TESTS`)

**Interfaces:**
- Consumes: `TradeQualifier`, `is_auction_trade`, `to_auction_type` (Task 1); `AuctionType` (`auction.hpp`); `TimeInForce`, `OrderRejectionReason` (`quarkbot/order_defs.hpp`).
- Produces (class `quarkbot::AuctionPhaseTracker`):
  - `using Timestamp = std::chrono::system_clock::time_point;`
  - `static constexpr std::chrono::system_clock::duration duplicate_window = std::chrono::minutes(2);`
  - `enum class Phase { continuous, call, closed };`
  - `struct Actions { bool uncross = false; std::optional<AuctionType> finish; bool close_day = false; bool start_close_timeout = false; };`
  - `Actions on_indicative(const std::string &name, AuctionType type);`
  - `Actions on_trade(const std::string &name, TradeQualifier qualifier, Timestamp time);`
  - `Actions on_close_timeout(const std::string &name);`
  - `bool can_match_continuous(const std::string &name) const;`
  - `bool is_closed(const std::string &name) const;`
  - `std::optional<OrderRejectionReason> check_auction_order(const std::string &name, TimeInForce tif) const;`
  - `Phase phase(const std::string &name) const;` (unknown instrument → `continuous`)
  - `AuctionType call_type(const std::string &name) const;` (meaningful in `call`, otherwise `unknown`)

The spec's `seen_auction` flag is represented by the presence of the instrument in the tracker's map: an instrument is stored only once it had an auction event.

- [ ] **Step 1: Write the failing test**

Create `src/tests/auction_phase_tracker_test.cpp`:

```cpp
#include "check.h"
#include "quarkbot/backtest/auction_phase_tracker.hpp"
#include <chrono>
#include <iostream>
#include <string>

using namespace quarkbot;
using Phase = AuctionPhaseTracker::Phase;
using Actions = AuctionPhaseTracker::Actions;
using namespace std::chrono_literals;

static const auto t0 = std::chrono::system_clock::from_time_t(1780670224);
static const std::string X = "X";
static const std::string Y = "Y";

static bool no_action(const Actions &a) {
    return !a.uncross && !a.finish && !a.close_day && !a.start_close_timeout;
}

static void test_unknown_instrument() {
    std::cout << "--- test_unknown_instrument" << std::endl;
    AuctionPhaseTracker tr;
    CHECK(tr.can_match_continuous(X));
    CHECK(!tr.is_closed(X));
    CHECK(tr.phase(X) == Phase::continuous);
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::not_tradable);
    CHECK(tr.check_auction_order(X, TimeInForce::atc) == OrderRejectionReason::not_tradable);
    CHECK(tr.check_auction_order(X, TimeInForce::crossing) == OrderRejectionReason::not_tradable);
    CHECK(no_action(tr.on_trade(X, TradeQualifier::continuous, t0)));
    //a continuous trade alone never makes the instrument an auction instrument
    CHECK(tr.check_auction_order(X, TimeInForce::atc) == OrderRejectionReason::not_tradable);
    CHECK(no_action(tr.on_close_timeout(X)));
}

static void test_indicative_unknown_ignored() {
    std::cout << "--- test_indicative_unknown_ignored" << std::endl;
    AuctionPhaseTracker tr;
    CHECK(no_action(tr.on_indicative(X, AuctionType::unknown)));
    CHECK(tr.can_match_continuous(X));
    CHECK(tr.check_auction_order(X, TimeInForce::atc) == OrderRejectionReason::not_tradable);
}

static void test_opening_with_indicative() {
    std::cout << "--- test_opening_with_indicative" << std::endl;
    AuctionPhaseTracker tr;
    CHECK(no_action(tr.on_indicative(X, AuctionType::opening)));
    CHECK(tr.phase(X) == Phase::call);
    CHECK(tr.call_type(X) == AuctionType::opening);
    CHECK(!tr.can_match_continuous(X));
    CHECK(!tr.check_auction_order(X, TimeInForce::ato).has_value());

    auto a = tr.on_trade(X, TradeQualifier::opening_auction, t0);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::opening);
    CHECK(!a.close_day);
    CHECK(!a.start_close_timeout);
    CHECK(tr.phase(X) == Phase::continuous);
    CHECK(tr.can_match_continuous(X));
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);
    CHECK(!tr.check_auction_order(X, TimeInForce::atc).has_value());
    CHECK(!tr.check_auction_order(X, TimeInForce::crossing).has_value());
}

static void test_opening_without_indicative() {
    std::cout << "--- test_opening_without_indicative" << std::endl;
    AuctionPhaseTracker tr;
    auto a = tr.on_trade(X, TradeQualifier::opening_auction, t0);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::opening);
    CHECK(tr.phase(X) == Phase::continuous);
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);
}

static void test_opening_did_not_happen() {
    std::cout << "--- test_opening_did_not_happen" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_indicative(X, AuctionType::opening);
    auto a = tr.on_trade(X, TradeQualifier::continuous, t0);
    CHECK(!a.uncross);
    CHECK(a.finish == AuctionType::opening);
    CHECK(!a.close_day);
    CHECK(tr.phase(X) == Phase::continuous);
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);
}

static void test_duplicate_opening() {
    std::cout << "--- test_duplicate_opening" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::opening_auction, t0);
    //a second print of the same opening (another market center) is a continuous trade
    CHECK(no_action(tr.on_trade(X, TradeQualifier::opening_auction, t0 + 1s)));
    CHECK(tr.phase(X) == Phase::continuous);
}

static void test_closing_with_indicative() {
    std::cout << "--- test_closing_with_indicative" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::opening_auction, t0);

    auto a = tr.on_indicative(X, AuctionType::closing);
    CHECK(a.start_close_timeout);
    CHECK(!a.uncross);
    CHECK(tr.phase(X) == Phase::call);
    CHECK(tr.call_type(X) == AuctionType::closing);
    CHECK(!tr.can_match_continuous(X));
    //the timeout is started once per day
    CHECK(no_action(tr.on_indicative(X, AuctionType::closing)));
    //a continuous trade in the closing call phase cannot happen on clean data - ignored
    CHECK(no_action(tr.on_trade(X, TradeQualifier::continuous, t0 + 1s)));
    CHECK(tr.phase(X) == Phase::call);

    a = tr.on_trade(X, TradeQualifier::closing_auction, t0 + 2s);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::closing);
    CHECK(a.close_day);
    CHECK(tr.is_closed(X));
    CHECK(tr.phase(X) == Phase::closed);
    CHECK(!tr.can_match_continuous(X));
    CHECK(tr.check_auction_order(X, TimeInForce::atc) == OrderRejectionReason::too_late);
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);

    //duplicate closing print: does nothing and must not reopen the day
    CHECK(no_action(tr.on_trade(X, TradeQualifier::closing_auction, t0 + 3s)));
    CHECK(tr.is_closed(X));
    //timeout after the uncross: nothing left to do
    CHECK(no_action(tr.on_close_timeout(X)));
    CHECK(tr.is_closed(X));
}

static void test_closing_without_indicative() {
    std::cout << "--- test_closing_without_indicative" << std::endl;
    AuctionPhaseTracker tr;
    auto a = tr.on_trade(X, TradeQualifier::closing_auction, t0);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::closing);
    CHECK(a.close_day);
    CHECK(!a.start_close_timeout);
    CHECK(tr.is_closed(X));
}

static void test_closing_timeout() {
    std::cout << "--- test_closing_timeout" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_indicative(X, AuctionType::closing);
    auto a = tr.on_close_timeout(X);
    CHECK(!a.uncross);
    CHECK(a.finish == AuctionType::closing);
    CHECK(a.close_day);
    CHECK(tr.is_closed(X));
    CHECK(no_action(tr.on_close_timeout(X)));
}

static void test_new_day_by_indicative_opening() {
    std::cout << "--- test_new_day_by_indicative_opening" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_indicative(X, AuctionType::closing);
    tr.on_trade(X, TradeQualifier::closing_auction, t0);
    CHECK(tr.is_closed(X));
    CHECK(no_action(tr.on_indicative(X, AuctionType::opening)));
    CHECK(!tr.is_closed(X));
    CHECK(tr.phase(X) == Phase::call);
    CHECK(tr.call_type(X) == AuctionType::opening);
    CHECK(!tr.check_auction_order(X, TimeInForce::ato).has_value());
    //the closing timeout is armed again on the new day
    CHECK(tr.on_indicative(X, AuctionType::closing).start_close_timeout);
}

static void test_new_day_by_opening_trade() {
    std::cout << "--- test_new_day_by_opening_trade" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::closing_auction, t0);
    auto a = tr.on_trade(X, TradeQualifier::opening_auction, t0 + 24h);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::opening);
    CHECK(!tr.is_closed(X));
    CHECK(tr.phase(X) == Phase::continuous);
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);
    CHECK(tr.on_indicative(X, AuctionType::closing).start_close_timeout);
}

static void test_new_day_by_continuous_trade() {
    std::cout << "--- test_new_day_by_continuous_trade" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::closing_auction, t0);
    CHECK(no_action(tr.on_trade(X, TradeQualifier::continuous, t0 + 24h)));
    CHECK(!tr.is_closed(X));
    CHECK(tr.can_match_continuous(X));
    //the opening auction was missed today
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);
    CHECK(!tr.check_auction_order(X, TimeInForce::atc).has_value());
    //a late opening print is a duplicate now
    CHECK(no_action(tr.on_trade(X, TradeQualifier::opening_auction, t0 + 24h + 1s)));
}

static void test_indicative_ignored_while_closed() {
    std::cout << "--- test_indicative_ignored_while_closed" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::closing_auction, t0);
    CHECK(no_action(tr.on_indicative(X, AuctionType::closing)));
    CHECK(no_action(tr.on_indicative(X, AuctionType::intraday)));
    CHECK(tr.is_closed(X));
}

static void test_intraday_window() {
    std::cout << "--- test_intraday_window" << std::endl;
    AuctionPhaseTracker tr;
    auto a = tr.on_trade(X, TradeQualifier::intraday_auction, t0);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::intraday);
    CHECK(!a.close_day);
    CHECK(tr.phase(X) == Phase::continuous);
    //inside the window: a duplicate print from another market center
    CHECK(no_action(tr.on_trade(X, TradeQualifier::intraday_auction, t0 + 119s)));
    //the window is measured from the last uncross, and its end is exclusive
    a = tr.on_trade(X, TradeQualifier::intraday_auction, t0 + 2min);
    CHECK(a.uncross);
    //another auction type has its own window
    a = tr.on_trade(X, TradeQualifier::unscheduled_auction, t0 + 2min + 1s);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::unscheduled);
    CHECK(no_action(tr.on_trade(X, TradeQualifier::unscheduled_auction, t0 + 2min + 30s)));
}

static void test_intraday_call_without_trade() {
    std::cout << "--- test_intraday_call_without_trade" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_indicative(X, AuctionType::intraday);
    CHECK(tr.phase(X) == Phase::call);
    auto a = tr.on_trade(X, TradeQualifier::continuous, t0);
    CHECK(!a.uncross);
    CHECK(a.finish == AuctionType::intraday);
    CHECK(tr.phase(X) == Phase::continuous);
    //an intraday auction says nothing about the opening auction
    CHECK(!tr.check_auction_order(X, TimeInForce::ato).has_value());
}

static void test_intraday_trade_keeps_closing_call() {
    std::cout << "--- test_intraday_trade_keeps_closing_call" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_indicative(X, AuctionType::closing);
    auto a = tr.on_trade(X, TradeQualifier::intraday_auction, t0);
    CHECK(a.uncross);
    CHECK(tr.phase(X) == Phase::call);
    CHECK(tr.call_type(X) == AuctionType::closing);
}

static void test_instruments_independent() {
    std::cout << "--- test_instruments_independent" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::closing_auction, t0);
    CHECK(tr.is_closed(X));
    CHECK(!tr.is_closed(Y));
    CHECK(tr.can_match_continuous(Y));
    CHECK(tr.check_auction_order(Y, TimeInForce::atc) == OrderRejectionReason::not_tradable);
}

int main() {
    test_unknown_instrument();
    test_indicative_unknown_ignored();
    test_opening_with_indicative();
    test_opening_without_indicative();
    test_opening_did_not_happen();
    test_duplicate_opening();
    test_closing_with_indicative();
    test_closing_without_indicative();
    test_closing_timeout();
    test_new_day_by_indicative_opening();
    test_new_day_by_opening_trade();
    test_new_day_by_continuous_trade();
    test_indicative_ignored_while_closed();
    test_intraday_window();
    test_intraday_call_without_trade();
    test_intraday_trade_keeps_closing_call();
    test_instruments_independent();
    return 0;
}
```

In `src/tests/CMakeLists.txt`, add to `BASIC_TESTS` right after `trade_qualifier.cpp`:

```cmake
    trade_qualifier.cpp
    auction_phase_tracker_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_auction_phase_tracker_test`
Expected: the compile fails with `'quarkbot/backtest/auction_phase_tracker.hpp' file not found`.

- [ ] **Step 3: Implement the header**

Create `src/quarkbot/backtest/auction_phase_tracker.hpp`:

```cpp
#pragma once

#include <quarkbot/order_defs.hpp>
#include <quarkbot/stream/auction.hpp>
#include <quarkbot/stream/trade.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

namespace quarkbot {

///Tracks the auction phase of every instrument for the simulator
/**
 * A pure state machine with no knowledge of orders. It takes market events
 * (indicative auctions, trades, the closing timeout) and returns the Actions
 * SimExecutor has to carry out on its orders.
 *
 * Keyed by instrument name, not by SimInstrument pointer: instances are cached
 * through a weak_ptr and can be recreated, while the name is the stable identity.
 *
 * An instrument is stored only once it had an auction event (indicative or
 * trade). Until then it trades continuously and auction orders are rejected.
 *
 * Input data is assumed clean: no pre-market, no after-market and no trades
 * during an auction call phase.
 */
class AuctionPhaseTracker {
public:
    using Timestamp = std::chrono::system_clock::time_point;

    ///An intraday or unscheduled print of the same type within this window
    ///after the last uncross is a duplicate (another market center).
    /** Real intraday auctions follow halts with pauses of about 5 minutes. */
    static constexpr std::chrono::system_clock::duration duplicate_window = std::chrono::minutes(2);

    enum class Phase {
        ///continuous trading - regular orders are matched
        continuous,
        ///call phase of an auction - nothing is matched, orders are collected
        call,
        ///the trading day is closed - nothing is matched
        closed
    };

    ///What SimExecutor has to do after an event, in this order
    struct Actions {
        ///run the uncross at the price and size of the trade just reported
        bool uncross = false;
        ///finish this auction - expire the unfilled auction orders valid for it
        std::optional<AuctionType> finish;
        ///close the trading day - expire every order except gtc
        bool close_day = false;
        ///start the closing timeout from the event's time
        bool start_close_timeout = false;
    };

    ///Indicative auction data arrived
    Actions on_indicative(const std::string &name, AuctionType type);
    ///A trade arrived (continuous or auction)
    Actions on_trade(const std::string &name, TradeQualifier qualifier, Timestamp time);
    ///The closing timeout started by on_indicative() expired
    Actions on_close_timeout(const std::string &name);

    ///true when regular orders may be matched against quotes and trades
    bool can_match_continuous(const std::string &name) const;
    ///true after the close of the trading day
    bool is_closed(const std::string &name) const;
    ///Decide whether an order with an auction time in force may rest
    /** @return rejection reason, or nullopt when the order may rest */
    std::optional<OrderRejectionReason> check_auction_order(const std::string &name, TimeInForce tif) const;

    ///current phase, continuous for an unknown instrument
    Phase phase(const std::string &name) const;
    ///auction type of the call phase, AuctionType::unknown outside of it
    AuctionType call_type(const std::string &name) const;

protected:

    struct State {
        Phase phase = Phase::continuous;
        AuctionType call_type = AuctionType::unknown;
        ///today's opening auction is over - whether it happened or not
        bool opening_done = false;
        ///today's closing timeout is already running
        bool close_timeout_started = false;
        std::optional<Timestamp> last_intraday_uncross;
        std::optional<Timestamp> last_unscheduled_uncross;
    };

    std::unordered_map<std::string, State> _states;

    static void new_day(State &st);
    ///leave the call phase of the given auction (a call phase of another auction is kept)
    static void end_call(State &st, AuctionType type);
    static Actions continuous_trade(State &st);
    static std::optional<Timestamp> &last_uncross(State &st, AuctionType type);
};

}
```

- [ ] **Step 4: Implement the source**

Create `src/quarkbot/backtest/auction_phase_tracker.cpp`:

```cpp
#include "auction_phase_tracker.hpp"

namespace quarkbot {

void AuctionPhaseTracker::new_day(State &st) {
    st.phase = Phase::continuous;
    st.call_type = AuctionType::unknown;
    st.opening_done = false;
    st.close_timeout_started = false;
}

void AuctionPhaseTracker::end_call(State &st, AuctionType type) {
    if (st.phase != Phase::call || st.call_type == type) {
        st.phase = Phase::continuous;
        st.call_type = AuctionType::unknown;
    }
}

std::optional<AuctionPhaseTracker::Timestamp> &AuctionPhaseTracker::last_uncross(State &st, AuctionType type) {
    return type == AuctionType::intraday ? st.last_intraday_uncross : st.last_unscheduled_uncross;
}

AuctionPhaseTracker::Actions AuctionPhaseTracker::continuous_trade(State &st) {
    Actions a;
    switch (st.phase) {
        case Phase::closed:
            //clean data has no after-market, so a trade after the close is the
            //next day - which started without an opening auction
            new_day(st);
            st.opening_done = true;
            break;
        case Phase::call:
            //a continuous trade in the closing call cannot happen on clean data
            if (st.call_type == AuctionType::closing) break;
            //the auction ended without an uncross
            a.finish = st.call_type;
            if (st.call_type == AuctionType::opening) st.opening_done = true;
            st.phase = Phase::continuous;
            st.call_type = AuctionType::unknown;
            break;
        case Phase::continuous:
            break;
    }
    return a;
}

AuctionPhaseTracker::Actions AuctionPhaseTracker::on_indicative(const std::string &name, AuctionType type) {
    Actions a;
    if (type == AuctionType::unknown) return a;
    State &st = _states[name];
    if (st.phase == Phase::closed) {
        //after the close only the next day's opening auction is meaningful
        if (type != AuctionType::opening) return a;
        new_day(st);
    }
    if (type == AuctionType::opening && st.opening_done) return a;
    st.phase = Phase::call;
    st.call_type = type;
    if (type == AuctionType::closing && !st.close_timeout_started) {
        //without a closing trade the indicative stream simply stops and it
        //stays quiet until the next day - the timeout closes the day then
        st.close_timeout_started = true;
        a.start_close_timeout = true;
    }
    return a;
}

AuctionPhaseTracker::Actions AuctionPhaseTracker::on_trade(const std::string &name, TradeQualifier qualifier, Timestamp time) {
    if (!is_auction_trade(qualifier)) {
        auto iter = _states.find(name);
        if (iter == _states.end()) return {};
        return continuous_trade(iter->second);
    }

    AuctionType type = to_auction_type(qualifier);
    State &st = _states[name];
    //one auction may arrive as several prints (multiple market centers). The
    //first one runs the auction, every following one is a continuous trade.
    switch (type) {
        case AuctionType::opening:
            if (st.phase == Phase::closed) new_day(st);
            if (st.opening_done) return continuous_trade(st);
            break;
        case AuctionType::closing:
            //a duplicate after the close must not reopen the day
            if (st.phase == Phase::closed) return {};
            break;
        default: {
            const auto &last = last_uncross(st, type);
            if (last && time - *last < duplicate_window) return continuous_trade(st);
            break;
        }
    }

    Actions a;
    a.uncross = true;
    a.finish = type;
    switch (type) {
        case AuctionType::opening:
            st.opening_done = true;
            end_call(st, type);
            break;
        case AuctionType::closing:
            st.phase = Phase::closed;
            st.call_type = AuctionType::unknown;
            a.close_day = true;
            break;
        default:
            last_uncross(st, type) = time;
            end_call(st, type);
            break;
    }
    return a;
}

AuctionPhaseTracker::Actions AuctionPhaseTracker::on_close_timeout(const std::string &name) {
    auto iter = _states.find(name);
    if (iter == _states.end()) return {};
    State &st = iter->second;
    if (st.phase == Phase::closed) return {};
    st.phase = Phase::closed;
    st.call_type = AuctionType::unknown;
    Actions a;
    a.finish = AuctionType::closing;
    a.close_day = true;
    return a;
}

bool AuctionPhaseTracker::can_match_continuous(const std::string &name) const {
    auto iter = _states.find(name);
    return iter == _states.end() || iter->second.phase == Phase::continuous;
}

bool AuctionPhaseTracker::is_closed(const std::string &name) const {
    auto iter = _states.find(name);
    return iter != _states.end() && iter->second.phase == Phase::closed;
}

std::optional<OrderRejectionReason> AuctionPhaseTracker::check_auction_order(const std::string &name, TimeInForce tif) const {
    auto iter = _states.find(name);
    //no auction seen yet - the instrument is not known to have auctions
    if (iter == _states.end()) return OrderRejectionReason::not_tradable;
    const State &st = iter->second;
    if (st.phase == Phase::closed) return OrderRejectionReason::too_late;
    if (tif == TimeInForce::ato && st.opening_done) return OrderRejectionReason::too_late;
    return std::nullopt;
}

AuctionPhaseTracker::Phase AuctionPhaseTracker::phase(const std::string &name) const {
    auto iter = _states.find(name);
    return iter == _states.end() ? Phase::continuous : iter->second.phase;
}

AuctionType AuctionPhaseTracker::call_type(const std::string &name) const {
    auto iter = _states.find(name);
    return iter == _states.end() ? AuctionType::unknown : iter->second.call_type;
}

}
```

In `src/quarkbot/backtest/CMakeLists.txt`, add the two files to `target_sources(quarkbot_backtest PRIVATE ...)` right before `simexecutor.cpp`:

```cmake
    auction_phase_tracker.cpp
    auction_phase_tracker.hpp
    simexecutor.cpp 
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build -j$(nproc) --target test_auction_phase_tracker_test && ./build/tests/test_auction_phase_tracker_test`
Expected: only `Passed:` lines, exit code 0.

- [ ] **Step 6: Commit**

```bash
git add src/quarkbot/backtest/auction_phase_tracker.hpp src/quarkbot/backtest/auction_phase_tracker.cpp src/quarkbot/backtest/CMakeLists.txt src/tests/auction_phase_tracker_test.cpp src/tests/CMakeLists.txt
git commit -m "backtest: AuctionPhaseTracker state machine

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `SimExecutor` driven by `AuctionPhaseTracker`

**Files:**
- Modify: `src/quarkbot/backtest/simexecutor.hpp`
- Modify: `src/quarkbot/backtest/simexecutor.cpp`
- Create: `src/tests/sim_auction_test.cpp`
- Modify: `src/tests/CMakeLists.txt` (`BASIC_TESTS`)

**Interfaces:**
- Consumes: `AuctionPhaseTracker` and its `Actions` (Task 2); `Trade::qualifier`, `to_auction_type` (Task 1); `is_valid_order_for_auction_type` (`auction.hpp`); `is_auction_order`, `is_limit_order` (`order_defs.hpp`); the file-local `stop_reached()` in `simexecutor.cpp`.
- Produces (private `SimExecutor` members, used only inside `simexecutor.cpp`):
  - `AuctionPhaseTracker _auctions;` (replaces `AuctionState` and `_auction_state`)
  - `bool admit_order(ActiveOrder &order);` (true = the order was terminated)
  - `bool match_on_placement(ActiveOrder &order);`
  - `void uncross(const PMarketInstrument &instrument, AuctionType type, Decimal price, Decimal size, Timestamp tp);`
  - `void finish_auction(const PMarketInstrument &instrument, AuctionType type);`
  - `void apply_auction_actions(const PMarketInstrument &instrument, const AuctionPhaseTracker::Actions &act, Timestamp tp);`

At this point the data sources still emit `Auction` with `final`. `SimExecutor` stops reading `final`, so such events count as indicative only. Tasks 4–6 move the sources over.

- [ ] **Step 1: Write the failing test**

Create `src/tests/sim_auction_test.cpp`:

```cpp
/// SimExecutor auction behaviour, end to end through BacktestEnv
/**
 * Each scenario replays a fixed list of events. The strategy places its orders
 * when it receives the first quote, and the report sink records the outcome of
 * every order by its label.
 */
#include "check.h"
#include "quarkbot/backtest/backtest.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/instrument_description.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/stream/auction.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/tradable_instrument.hpp"
#include "quarkbot/types.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace quarkbot;
using namespace std::chrono_literals;

namespace {

using Event = std::variant<Quote, Trade, Auction>;

struct TimedEvent {
    ///offset from the start of the scenario
    std::chrono::seconds at;
    Event ev;
};

struct FillRec {
    Decimal price;
    Decimal quantity;
};

struct Outcome {
    std::vector<FillRec> fills;
    ///last plain status reported (filled, canceled...)
    std::optional<OrderStatus> status;
    ///termination reason (expired, too_late...)
    std::optional<OrderRejectionReason> reason;
    bool terminated() const {
        return reason.has_value()
            || (status.has_value() && *status != OrderStatus::open);
    }
};

using Outcomes = std::map<std::string, Outcome>;

struct Scenario {
    std::vector<TimedEvent> events;
    ///placed when the strategy receives the first quote; labels must be unique
    std::vector<OrderRequest> orders;
};

Quote quote(Decimal bid, Decimal ask) {
    return Quote{bid, 10_dec, ask, 10_dec, {}};
}

Trade trade(Decimal price, Decimal size, TradeQualifier q = TradeQualifier::continuous) {
    return Trade{price, size, {}, Side::undetermined, q};
}

Auction indicative(AuctionType type, Decimal price) {
    Auction a;
    a.price = price;
    a.quantity = 100_dec;
    a.imbalance = 0_dec;
    a.auction_type = type;
    return a;
}

OrderRequest buy(std::string label, OrderType type, Decimal qty, TimeInForce tif,
                 Decimal limit = {}, Decimal stop = {}) {
    return OrderRequest{
        .label = std::move(label), .side = Side::buy, .type = type, .quantity = qty,
        .limit_price = limit, .stop_price = stop, .time_in_force = tif
    };
}

InstrumentDescription make_desc() {
    return InstrumentDescription{
        {},
        {0.00001_dec, Decimal::max(), 0.00001_dec, 0.01_dec, 0, 0, 0, 0},
        {"USD"}, {"USD"}, {}, "TESTI", InstrumentCategory::Crypto
    };
}

StrategyFragment strategy(StrategyContext ctx, const Scenario *sc) {
    TradableInstrument instr = ctx.instruments.at(0);
    auto stream = instr.subscribe<Quote>();
    Quote qt;
    if (!co_await stream.receive(qt)) co_return;
    //keep the instances alive - a dropped order instance is canceled
    std::vector<Order> keep;
    for (auto &r: sc->orders) keep.push_back(instr.place_order(r));
    co_await ctx.stop_signal;
}

Outcomes run_in_thread(const Scenario &sc) {
    auto t0 = std::chrono::system_clock::from_time_t(1780670224);
    Outcomes out;

    std::array<WalletInitItem, 1> wallet{WalletInitItem{"USD", 100000000_dec}};
    std::array<InstrumentDescription, 1> instruments{make_desc()};

    SimulationParams sim;
    sim.slippage = 0;
    sim.reporter = ReportSink([&](const Order &ord, const OrderStatusUpdate &upd){
        Outcome &o = out[ord.get_parameters().label];
        if (std::holds_alternative<Fill>(upd)) {
            const Fill &f = std::get<Fill>(upd);
            o.fills.push_back({f.price, f.quantity});
        } else if (std::holds_alternative<OrderStatus>(upd)) {
            o.status = std::get<OrderStatus>(upd);
        } else if (std::holds_alternative<OrderRejectionReason>(upd)) {
            o.reason = std::get<OrderRejectionReason>(upd);
        } else if (std::holds_alternative<OrderRejectionWithText>(upd)) {
            o.reason = std::get<OrderRejectionWithText>(upd).reason;
        }
    });

    BacktestEnv bt("backtest", wallet, instruments, sim);
    bt.add_strategy([&](StrategyContext &&c){return strategy(std::move(c), &sc);});

    std::size_t n = 0;
    bt.run([&](BacktestEvent &ev) {
        if (n >= sc.events.size()) return false;
        const TimedEvent &te = sc.events[n++];
        ev.symbol = "TESTI";
        ev.time = t0 + te.at;
        std::visit([&](auto e){
            e.time = ev.time;
            ev.data = e;
        }, te.ev);
        return true;
    });
    return out;
}

///BacktestExecutor is bound to the thread that created it, so each scenario
///needs a fresh thread of its own
Outcomes run(const Scenario &sc) {
    Outcomes out;
    std::thread thr([&]{ out = run_in_thread(sc); });
    thr.join();
    return out;
}

void check_fills(const Outcomes &out, const std::string &label, const std::vector<FillRec> &expected) {
    std::cout << "    order " << label << std::endl;
    auto iter = out.find(label);
    CHECK(iter != out.end());
    const auto &fills = iter->second.fills;
    CHECK_EQUAL(fills.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK_EQUAL(fills[i].price, expected[i].price);
        CHECK_EQUAL(fills[i].quantity, expected[i].quantity);
    }
}

const Outcome &get(const Outcomes &out, const std::string &label) {
    auto iter = out.find(label);
    CHECK(iter != out.end());
    return iter->second;
}

}

///Algoseek / IQFeed style: no indicative data, the opening print is the auction
static void test_opening_without_indicative() {
    std::cout << "--- test_opening_without_indicative" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,   trade(99_dec, 100_dec, TradeQualifier::closing_auction)},   //previous close
        {60s,  quote(100_dec, 101_dec)},                                   //closed: orders placed
        {120s, trade(100_dec, 5_dec, TradeQualifier::opening_auction)},    //uncross at 100, S = 5
        {121s, trade(100_dec, 3_dec, TradeQualifier::opening_auction)},    //duplicate print
        {122s, quote(100_dec, 101_dec)},
    };
    sc.orders = {
        buy("ato_mkt", OrderType::market, 7_dec, TimeInForce::ato),
        buy("ato_lim_miss", OrderType::limit, 5_dec, TimeInForce::ato, 99_dec),
        buy("atc_lim", OrderType::limit, 5_dec, TimeInForce::atc, 105_dec),
        buy("gtc_lim", OrderType::limit, 8_dec, TimeInForce::gtc, 102_dec),
        buy("gtc_mkt", OrderType::market, 20_dec, TimeInForce::gtc),
        buy("gtc_stop", OrderType::stop, 3_dec, TimeInForce::gtc, {}, 99.5_dec),
        buy("alert", OrderType::alert, 1_dec, TimeInForce::gtc, {}, 99.5_dec),
        buy("day_lim", OrderType::limit, 1_dec, TimeInForce::day, 102_dec),
    };
    auto out = run(sc);

    //auction order: full quantity at the auction price, no volume cap for market
    check_fills(out, "ato_mkt", {{100_dec, 7_dec}});
    CHECK(get(out, "ato_mkt").status == OrderStatus::filled);
    //limit below the auction price: no fill, expired with the auction
    check_fills(out, "ato_lim_miss", {});
    CHECK(get(out, "ato_lim_miss").reason == OrderRejectionReason::expired);
    //closing order: neither the opening nor the duplicate continuous print touch it
    check_fills(out, "atc_lim", {});
    CHECK(!get(out, "atc_lim").terminated());
    //regular limit: capped by the first print at the auction price, then the
    //duplicate is a continuous trade filled at the limit price
    check_fills(out, "gtc_lim", {{100_dec, 5_dec}, {102_dec, 3_dec}});
    //regular market: no volume cap
    check_fills(out, "gtc_mkt", {{100_dec, 20_dec}});
    //stop: triggered by the auction price, filled in continuous trading at the ask
    check_fills(out, "gtc_stop", {{101_dec, 3_dec}});
    //alert: triggered by the auction price, no fill
    check_fills(out, "alert", {});
    CHECK(get(out, "alert").status == OrderStatus::filled);
    //day order while the day is closed
    check_fills(out, "day_lim", {});
    CHECK(get(out, "day_lim").reason == OrderRejectionReason::too_late);
}

///indicative data drives the call phase - nothing is matched until the uncross
static void test_call_phase() {
    std::cout << "--- test_call_phase" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,  trade(99_dec, 100_dec, TradeQualifier::closing_auction)},
        {60s, indicative(AuctionType::opening, 100_dec)},                  //new day, call
        {61s, quote(100_dec, 101_dec)},                                    //orders placed
        {62s, quote(100_dec, 101_dec)},                                    //still call
        {64s, trade(100.5_dec, 10_dec, TradeQualifier::opening_auction)},
    };
    sc.orders = {
        buy("gtc_lim", OrderType::limit, 4_dec, TimeInForce::gtc, 102_dec),
        buy("ioc_lim", OrderType::limit, 1_dec, TimeInForce::ioc, 102_dec),
        buy("ato_lim", OrderType::limit, 2_dec, TimeInForce::ato, 101_dec),
    };
    auto out = run(sc);

    //would cross the ask 101 in continuous trading - in the call phase it waits
    check_fills(out, "gtc_lim", {{100.5_dec, 4_dec}});
    check_fills(out, "ioc_lim", {});
    CHECK(get(out, "ioc_lim").reason == OrderRejectionReason::not_filled);
    check_fills(out, "ato_lim", {{100.5_dec, 2_dec}});
    CHECK(get(out, "ato_lim").status == OrderStatus::filled);
}

///the opening call ends with a continuous trade - the auction did not happen
static void test_opening_did_not_happen() {
    std::cout << "--- test_opening_did_not_happen" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,  trade(99_dec, 100_dec, TradeQualifier::closing_auction)},
        {60s, indicative(AuctionType::opening, 100_dec)},
        {61s, quote(100_dec, 101_dec)},
        {62s, trade(101_dec, 5_dec)},
        {63s, quote(100_dec, 101_dec)},
    };
    sc.orders = {
        buy("ato_lim", OrderType::limit, 2_dec, TimeInForce::ato, 101_dec),
        buy("gtc_lim", OrderType::limit, 1_dec, TimeInForce::gtc, 102_dec),
    };
    auto out = run(sc);

    check_fills(out, "ato_lim", {});
    CHECK(get(out, "ato_lim").reason == OrderRejectionReason::expired);
    //the first continuous trade is matched as usual - at the limit price
    check_fills(out, "gtc_lim", {{102_dec, 1_dec}});
}

///closing auction with indicative data, close of the day, duplicate print
static void test_closing_auction() {
    std::cout << "--- test_closing_auction" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,  trade(99_dec, 100_dec, TradeQualifier::opening_auction)},
        {1s,  quote(100_dec, 101_dec)},                                    //orders placed
        {10s, indicative(AuctionType::closing, 100_dec)},
        {20s, trade(100_dec, 50_dec, TradeQualifier::closing_auction)},
        {21s, trade(100_dec, 50_dec, TradeQualifier::closing_auction)},    //duplicate
        {30s, quote(80_dec, 85_dec)},                                      //closed
    };
    sc.orders = {
        buy("atc_mkt", OrderType::market, 3_dec, TimeInForce::atc),
        buy("ato_late", OrderType::market, 1_dec, TimeInForce::ato),
        buy("day_far", OrderType::limit, 1_dec, TimeInForce::day, 90_dec),
        buy("gtc_far", OrderType::limit, 1_dec, TimeInForce::gtc, 90_dec),
    };
    auto out = run(sc);

    check_fills(out, "atc_mkt", {{100_dec, 3_dec}});
    CHECK(get(out, "atc_mkt").status == OrderStatus::filled);
    check_fills(out, "ato_late", {});
    CHECK(get(out, "ato_late").reason == OrderRejectionReason::too_late);
    //close_day expires day orders
    check_fills(out, "day_far", {});
    CHECK(get(out, "day_far").reason == OrderRejectionReason::expired);
    //gtc survives, and the crossing quote after the close does not fill it
    check_fills(out, "gtc_far", {});
    CHECK(!get(out, "gtc_far").terminated());
}

///without a closing trade the timeout closes the day
static void test_closing_timeout() {
    std::cout << "--- test_closing_timeout" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,          trade(99_dec, 100_dec, TradeQualifier::opening_auction)},
        {1s,          quote(100_dec, 101_dec)},                            //orders placed
        {10s,         indicative(AuctionType::closing, 100_dec)},
        {29min,       quote(100_dec, 101_dec)},
        {31min,       quote(100_dec, 101_dec)},                            //after the timeout
    };
    sc.orders = {
        buy("day_far", OrderType::limit, 1_dec, TimeInForce::day, 90_dec),
        buy("atc_lim", OrderType::limit, 1_dec, TimeInForce::atc, 95_dec),
    };
    auto out = run(sc);

    check_fills(out, "day_far", {});
    CHECK(get(out, "day_far").reason == OrderRejectionReason::expired);
    check_fills(out, "atc_lim", {});
    CHECK(get(out, "atc_lim").reason == OrderRejectionReason::expired);
}

///an unscheduled auction runs once; a duplicate print within the window is continuous
static void test_unscheduled_duplicate() {
    std::cout << "--- test_unscheduled_duplicate" << std::endl;
    Scenario sc;
    sc.events = {
        {0s,  trade(100_dec, 10_dec, TradeQualifier::opening_auction)},
        {1s,  quote(100_dec, 101_dec)},                                    //orders placed
        {10s, trade(99_dec, 4_dec, TradeQualifier::unscheduled_auction)},
        {20s, trade(99_dec, 4_dec, TradeQualifier::unscheduled_auction)},  //duplicate
    };
    sc.orders = {
        buy("crs_lim", OrderType::limit, 10_dec, TimeInForce::crossing, 100_dec),
        buy("gtc_lim", OrderType::limit, 10_dec, TimeInForce::gtc, 99.5_dec),
    };
    auto out = run(sc);

    //crossing joins any auction; the remainder expires with it
    check_fills(out, "crs_lim", {{99_dec, 4_dec}});
    CHECK(get(out, "crs_lim").reason == OrderRejectionReason::expired);
    //auction price first, then the duplicate as a continuous trade at the limit
    check_fills(out, "gtc_lim", {{99_dec, 4_dec}, {99.5_dec, 4_dec}});
}

///an instrument without any auction event trades exactly as before
static void test_no_auctions() {
    std::cout << "--- test_no_auctions" << std::endl;
    Scenario sc;
    sc.events = {
        {0s, quote(100_dec, 101_dec)},                                     //orders placed
        {1s, quote(100_dec, 101_dec)},
    };
    sc.orders = {
        buy("ato_mkt", OrderType::market, 1_dec, TimeInForce::ato),
        buy("gtc_lim", OrderType::limit, 1_dec, TimeInForce::gtc, 102_dec),
    };
    auto out = run(sc);

    check_fills(out, "ato_mkt", {});
    CHECK(get(out, "ato_mkt").reason == OrderRejectionReason::not_tradable);
    check_fills(out, "gtc_lim", {{101_dec, 1_dec}});
}

int main() {
    test_opening_without_indicative();
    test_call_phase();
    test_opening_did_not_happen();
    test_closing_auction();
    test_closing_timeout();
    test_unscheduled_duplicate();
    test_no_auctions();
    return 0;
}
```

In `src/tests/CMakeLists.txt`, add to `BASIC_TESTS` right after `sim_last_quote_per_instrument.cpp`:

```cmake
    sim_last_quote_per_instrument.cpp
    sim_auction_test.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_sim_auction_test && ./build/tests/test_sim_auction_test`
Expected: it builds (only existing APIs are used) and fails in `test_opening_without_indicative`. The current executor never runs an uncross on a `Trade`, so `ato_mkt` has no fill (`FAILED: fills.size()==expected.size(): 0==1`).

- [ ] **Step 3: Update the header**

In `src/quarkbot/backtest/simexecutor.hpp`:

Add the include after `#include "quarkbot/backtest/isimexecutor.hpp"`:

```cpp
#include "auction_phase_tracker.hpp"
```

Replace the whole `struct AuctionState { ... };` block **and** the line `std::unordered_map<std::string, AuctionState> _auction_state;` with:

```cpp
    ///auction phase of every instrument - decides when orders may be matched
    AuctionPhaseTracker _auctions;
```

In the `_last_quote` doc comment, replace `- same as _auction_state.` with `- same as _auctions.`

Below the declaration of `void close_day(PMarketInstrument instrument);`, add:

```cpp
    ///Check an order against the auction phase on placement
    /** @retval true the order was terminated and must not be kept */
    bool admit_order(ActiveOrder &order);
    ///Match an order right on placement, when the phase allows it
    /** @retval true the order is done */
    bool match_on_placement(ActiveOrder &order);
    ///Execute an auction of the given type at a single price
    /** @param size size of the auction print - caps limit orders only */
    void uncross(const PMarketInstrument &instrument, AuctionType type, Decimal price, Decimal size, Timestamp tp);
    ///The auction is over - expire the auction orders valid for it
    void finish_auction(const PMarketInstrument &instrument, AuctionType type);
    void apply_auction_actions(const PMarketInstrument &instrument, const AuctionPhaseTracker::Actions &act, Timestamp tp);
```

- [ ] **Step 4: Rewrite order placement**

In `src/quarkbot/backtest/simexecutor.cpp`, inside `place_new_order_internal`, replace:

```cpp
        if (!validate_order(aord)) return;

        accept_order(aord.ord);

        if (match_order(aord,true)) return;
        if (finish_if_cannot_rest(aord)) return;

        _active_orders.push_back(std::move(aord));
```

with:

```cpp
        if (!validate_order(aord)) return;

        accept_order(aord.ord);

        if (admit_order(aord)) return;
        if (match_on_placement(aord)) return;
        if (finish_if_cannot_rest(aord)) return;

        _active_orders.push_back(std::move(aord));
```

Inside `place_order_internal`, replace:

```cpp
        if (match_order(aord,true) || finish_if_cannot_rest(aord)) {
```

with:

```cpp
        if (admit_order(aord) || match_on_placement(aord) || finish_if_cannot_rest(aord)) {
```

Add these two functions right after `finish_if_cannot_rest`:

```cpp
    bool SimExecutor::admit_order(ActiveOrder &order) {
        const auto &name = order.instrument->get_info().name;
        const auto &params = order.ord->get_parameters();
        if (is_auction_order(params.time_in_force)) {
            //an auction order only waits for its auction - never matched now
            auto rejection = _auctions.check_auction_order(name, params.time_in_force);
            if (rejection) {
                set_order_status(order.ord, *rejection);
                return true;
            }
            return false;
        }
        if (params.time_in_force == TimeInForce::day && _auctions.is_closed(name)) {
            set_order_status(order.ord, OrderRejectionReason::too_late);
            return true;
        }
        return false;
    }

    bool SimExecutor::match_on_placement(ActiveOrder &order) {
        if (is_auction_order(order.time_in_force)) return false;
        //in a call phase or after the close the order waits - an ioc/fok is
        //then canceled by finish_if_cannot_rest()
        if (!_auctions.can_match_continuous(order.instrument->get_info().name)) return false;
        return match_order(order, true);
    }
```

- [ ] **Step 5: Remove the auction checks from quote matching**

In `SimExecutor::match_order(ActiveOrder &order, Quote quote, bool taker)`, replace everything from the start of the body up to (not including) `if (real_order_type(order) == OrderType::alert) {`:

```cpp
        auto &params  = order.ord->get_parameters();
        //attempt to find auction information about this instrument
        const auto &info  = order.ord->get_instrument()->get_instrument()->get_info();        
        auto auction_state_iter = _auction_state.find(info.name);        

        //if order is auction order
        if (is_auction_order(params.time_in_force)) {            
            ...
            //keep order
            return false;
        }
        if (params.time_in_force == TimeInForce::day  && auction_state_iter != _auction_state.end() 
                    && auction_state_iter->second.at_close_finished) {
            set_order_status(order.ord,OrderRejectionReason::too_late);
            return true;
        }        
```

with just:

```cpp
        //auction orders and the phase of the day are handled before matching
        //(admit_order, can_match_continuous) - this only does the matching math
        auto &params  = order.ord->get_parameters();
```

- [ ] **Step 6: Rewrite the event handlers**

Replace the whole `void SimExecutor::on_event(PMarketInstrument instrument, Trade &trade){ ... }` with:

```cpp
    void SimExecutor::on_event(PMarketInstrument instrument, Trade &trade){
        seed_random(trade.time);
        const auto &name = instrument->get_info().name;
        auto act = _auctions.on_trade(name, trade.qualifier, trade.time);
        if (act.uncross) {
            uncross(instrument, to_auction_type(trade.qualifier), trade.price, trade.size, trade.time);
        }
        apply_auction_actions(instrument, act, trade.time);
        //an uncross is the auction itself, it is not matched once more as a trade
        if (act.uncross || !_auctions.can_match_continuous(name)) return;
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord){
            if (ord.instrument == instrument && !is_auction_order(ord.time_in_force)) {
                return match_order(ord, trade);
            }
            return false;
        });
        _active_orders.erase(e, _active_orders.end());
    }
```

In `void SimExecutor::on_event(PMarketInstrument instrument, Quote &quote)`, replace:

```cpp
        Quote book = with_normalized_sizes(quote);
        _last_quote[instrument->get_info().name] = book;
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord){
            if (ord.instrument == instrument) {
                bool b =  match_order(ord, book, false);
                if (b) return true;
```

with:

```cpp
        Quote book = with_normalized_sizes(quote);
        const auto &name = instrument->get_info().name;
        _last_quote[name] = book;
        //the quote is remembered always, but only matched in continuous trading
        bool can_match = _auctions.can_match_continuous(name);
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord){
            //auction orders are not in the continuous book
            if (ord.instrument == instrument && !is_auction_order(ord.time_in_force)) {
                if (can_match && match_order(ord, book, false)) return true;
```

(The rest of the lambda, which reflects resting limits in `new_quote`, stays as is.)

Replace the whole `void SimExecutor::on_event(PMarketInstrument instrument, Auction &auction_data) { ... }` with the new handler and the three helpers:

```cpp
    void SimExecutor::on_event(PMarketInstrument instrument, Auction &auction_data) {
        seed_random(auction_data.time);
        //indicative data only drives the phase - it never fills anything
        auto act = _auctions.on_indicative(instrument->get_info().name, auction_data.auction_type);
        apply_auction_actions(instrument, act, auction_data.time);
    }

    void SimExecutor::apply_auction_actions(const PMarketInstrument &instrument, const AuctionPhaseTracker::Actions &act, Timestamp tp) {
        if (act.finish) finish_auction(instrument, *act.finish);
        if (act.close_day) close_day(instrument);
        if (act.start_close_timeout) expire_auction(instrument, tp);
    }

    void SimExecutor::uncross(const PMarketInstrument &instrument, AuctionType type, Decimal price, Decimal size, Timestamp tp) {
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord) {
            if (ord.instrument != instrument) return false;
            const auto &params = ord.ord->get_parameters();
            //an auction order takes part in its own auction only
            if (is_auction_order(params.time_in_force)
                    && !is_valid_order_for_auction_type(params.time_in_force, type)) {
                return false;
            }
            auto otype = real_order_type(ord);
            if (otype == OrderType::stop || otype == OrderType::alert) {
                //the auction price triggers stops, but a triggered stop joins
                //the next phase, not this uncross - same as on a real venue
                if (!stop_reached(params.stop_price, price, params.side)) return false;
                if (otype == OrderType::alert) {
                    set_order_status(ord.ord, {OrderStatus::filled});
                    return true;
                }
                ord.trig = true;
                return false;
            }
            Decimal leave_quant = params.quantity - ord.calcs.filled;
            Decimal fill_quant;
            if (otype == OrderType::market) {
                //the simulator is not a market participant - the printed
                //volume does not limit a market order
                fill_quant = leave_quant;
            } else if (is_limit_order(otype)) {
                bool crosses = (params.side == Side::buy && params.limit_price >= price)
                            || (params.side == Side::sell && params.limit_price <= price);
                if (!crosses) return false;
                //every participant gets the single auction price; the print
                //size roughly models a partial execution
                fill_quant = std::min(leave_quant, size);
            } else {
                return false;
            }
            if (fill_quant > 0) create_fill(ord, price, fill_quant, tp, true);
            if (ord.calcs.filled >= params.quantity) {
                set_order_status(ord.ord, {OrderStatus::filled});
                return true;
            }
            return false;
        });
        _active_orders.erase(e, _active_orders.end());
    }

    void SimExecutor::finish_auction(const PMarketInstrument &instrument, AuctionType type) {
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord) {
            if (ord.instrument != instrument) return false;
            auto tif = ord.ord->get_parameters().time_in_force;
            if (!is_auction_order(tif) || !is_valid_order_for_auction_type(tif, type)) return false;
            //the auction is over, so an unfilled remainder can no longer trade -
            //that is expired, not filled
            set_order_status(ord.ord, OrderRejectionReason::expired);
            return true;
        });
        _active_orders.erase(e, _active_orders.end());
    }
```

Replace the whole `StrategyFragment SimExecutor::expire_auction(...) { ... }` with:

```cpp
StrategyFragment SimExecutor::expire_auction(PMarketInstrument instrument, std::chrono::system_clock::time_point tp) {
    if (co_await _timer.sleep_until(tp+std::chrono::minutes(30))) {
        auto act = _auctions.on_close_timeout(instrument->get_info().name);
        apply_auction_actions(instrument, act, tp);
    }
}
```

- [ ] **Step 7: Run the new test**

Run: `cmake --build build -j$(nproc) --target test_sim_auction_test && ./build/tests/test_sim_auction_test`
Expected: only `Passed:` lines, exit code 0.

If `test_closing_timeout` fails because `day_far` is not expired, check whether `BacktestEnv` fires the timer before the event at `+31min`. Do not change the assertion; investigate with superpowers:systematic-debugging.

- [ ] **Step 8: Run the full suite**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: all tests pass. `algoseek_source_test` and `replay_csv` are unaffected (they only read sources). `sim_order_type_math` always exits 0 (it is an audit tool): compare its output with `git stash`-ed output only if something looks different.

- [ ] **Step 9: Commit**

```bash
git add src/quarkbot/backtest/simexecutor.hpp src/quarkbot/backtest/simexecutor.cpp src/tests/sim_auction_test.cpp src/tests/CMakeLists.txt
git commit -m "backtest: SimExecutor runs auctions from qualified trades

Phases come from AuctionPhaseTracker: no continuous matching during a
call phase or after the close, a single-price uncross for auction and
regular orders, stops triggered by the auction price.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Algoseek emits auction prints as `Trade`

**Files:**
- Modify: `src/quarkbot/algoseek/algoseek_data_source.cpp:283-317`
- Modify: `src/quarkbot/algoseek/algoseek_data_source.hpp:40-49`
- Modify: `src/quarkbot/backtest/config_datasource.hpp:27`
- Test: `src/tests/algoseek_source_test.cpp`

**Interfaces:**
- Consumes: `TradeQualifier`, `to_trade_qualifier`, `is_auction_trade` (Task 1).
- Produces: `AlgoseekDataSource` emits only `Trade` (plus fake `Quote`s). Opening, closing and reopening prints carry `opening_auction`, `closing_auction` and `unscheduled_auction`. The counter `_counters.auctions` counts auction trades.

- [ ] **Step 1: Update the tests**

In `src/tests/algoseek_source_test.cpp`, `test_auctions_and_skips`, replace the block from `// 1: opening auction` through the end of the `// 5: closing auction ...` block with:

```cpp
        // 1: opening auction print
        CHECK(std::holds_alternative<Trade>(evs[0].data));
        {
            auto &t = std::get<Trade>(evs[0].data);
            CHECK(t.qualifier == TradeQualifier::opening_auction);
            CHECK(t.price == Decimal::from_string("47.58"));
            CHECK(t.size == Decimal::from_string("4000"));
            CHECK(t.side == Side::undetermined);
            CHECK(t.time == mk_utc("2023-06-09 13:30:00.791480832"));
        }

        // 2: ordinary trade (the official open row in between was dropped)
        CHECK(std::holds_alternative<Trade>(evs[1].data));
        CHECK(std::get<Trade>(evs[1].data).qualifier == TradeQualifier::continuous);
        CHECK(std::get<Trade>(evs[1].data).price == Decimal::from_string("47.60"));

        // 3: reopening auction maps to unscheduled
        CHECK(std::holds_alternative<Trade>(evs[2].data));
        CHECK(std::get<Trade>(evs[2].data).qualifier == TradeQualifier::unscheduled_auction);

        // 4: the cancelled trade survives
        CHECK(std::holds_alternative<Trade>(evs[3].data));
        CHECK(std::get<Trade>(evs[3].data).qualifier == TradeQualifier::continuous);
        CHECK(std::get<Trade>(evs[3].data).size == Decimal::from_string("42"));

        // 5: closing auction wins over the official_close bit on the same row
        CHECK(std::holds_alternative<Trade>(evs[4].data));
        {
            auto &t = std::get<Trade>(evs[4].data);
            CHECK(t.qualifier == TradeQualifier::closing_auction);
            CHECK(t.size == Decimal::from_string("23455"));
            CHECK(t.time == mk_utc("2023-06-09 20:00:02.164273920"));
        }
```

Right after that block (still inside `test_auctions_and_skips`, after the closing `}` of the `{ AlgoseekDataSource src(...) ... }` scope), add the fake quote check:

```cpp
    // fake quotes follow continuous trades only, never an auction print
    {
        AlgoseekSpec spec{path,{},std::chrono::locate_zone("America/New_York")};
        spec.fake_quotes_distance = Decimal::from_string("0.01");
        AlgoseekDataSource src(spec);
        auto evs = drain(src);
        // the 5 events above plus one quote after each of the 2 continuous trades
        CHECK_EQUAL(evs.size(), std::size_t(7));
        for (std::size_t i = 0; i < evs.size(); ++i) {
            if (!std::holds_alternative<Quote>(evs[i].data)) continue;
            CHECK(i > 0);
            CHECK(std::holds_alternative<Trade>(evs[i-1].data));
            CHECK(std::get<Trade>(evs[i-1].data).qualifier == TradeQualifier::continuous);
        }
    }
```

If `Quote` is not yet visible in this test file, add `#include "quarkbot/stream/quote.hpp"` to its includes.

In `count_events`, replace:

```cpp
        if (std::holds_alternative<Trade>(ev.data)) {
            ++c.trades;
        } else if (std::holds_alternative<Auction>(ev.data)) {
            switch (std::get<Auction>(ev.data).auction_type) {
```

with:

```cpp
        if (!std::holds_alternative<Trade>(ev.data)) continue;
        const auto &t = std::get<Trade>(ev.data);
        if (!is_auction_trade(t.qualifier)) {
            ++c.trades;
        } else {
            switch (to_auction_type(t.qualifier)) {
```

(The `case` lines and closing braces stay as they are.)

In `test_real_files`, the last block: replace `const Auction *opening = nullptr;` / `const Auction *closing = nullptr;` with `const Trade *opening = nullptr;` / `const Trade *closing = nullptr;`. Replace:

```cpp
            if (!std::holds_alternative<Auction>(ev.data)) continue;
            const auto &a = std::get<Auction>(ev.data);
            if (a.auction_type == AuctionType::opening) {
```

with:

```cpp
            if (!std::holds_alternative<Trade>(ev.data)) continue;
            const auto &a = std::get<Trade>(ev.data);
            if (a.qualifier == TradeQualifier::opening_auction) {
```

and `if (a.auction_type == AuctionType::closing) {` with `if (a.qualifier == TradeQualifier::closing_auction) {`. Replace `opening->quantity` with `opening->size` and `closing->quantity` with `closing->size`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_algoseek_source_test && ./build/tests/test_algoseek_source_test`
Expected: `FAILED: std::holds_alternative<Trade>(evs[0].data)` (the source still emits `Auction`).

- [ ] **Step 3: Implement**

In `src/quarkbot/algoseek/algoseek_data_source.cpp`, replace the whole `if (auction_type != AuctionType::unknown) { ... } else { ... }` block (from `if (auction_type != AuctionType::unknown) {` to the `}` closing the `else` branch, just before `return true;`) with:

```cpp
        Trade &t = ev.data.emplace<Trade>();
        t.price = price;
        t.size = quantity;
        t.time = time;
        //the export carries no aggressor side
        t.side = Side::undetermined;
        //an auction print is the auction result itself - the export carries
        //no indicative auction data
        t.qualifier = to_trade_qualifier(auction_type);
        if (is_auction_trade(t.qualifier)) {
            ++_counters.auctions;
        } else {
            ++_counters.trades;

            if (_spec.fake_quotes_distance) {
                Quote q;
                _next_quote.emplace();
                q.ask =  t.price + *_spec.fake_quotes_distance;
                q.bid =  t.price - *_spec.fake_quotes_distance;
                q.ask_size = t.size;
                q.bid_size = t.size;
                q.time = t.time;
                _next_quote->data = q;
                _next_quote->symbol = ev.symbol;
                _next_quote->time = ev.time;
            }
        }
```

In `src/quarkbot/algoseek/algoseek_data_source.hpp`, replace:

```cpp
///Backtest data source generating Trade and Auction events from an Algoseek trades export
/**
    Reads one gzipped CSV file with the eight column layout
    Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange,Conditions

    Only Trade and final Auction events are produced; the format carries no
    quotes and no indicative auction data. Rows that do not represent a
```

with:

```cpp
///Backtest data source generating Trade events from an Algoseek trades export
/**
    Reads one gzipped CSV file with the eight column layout
    Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange,Conditions

    Only Trade events are produced; opening, closing and reopening prints are
    Trade events with an auction qualifier. The format carries no quotes and no
    indicative auction data. Rows that do not represent a
```

In `src/quarkbot/backtest/config_datasource.hpp`, replace `ticker per day) and produces Trade and final Auction events. It is configured by` with `ticker per day) and produces Trade events (auction prints carry an auction qualifier). It is configured by`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build -j$(nproc) --target test_algoseek_source_test test_config_datasource_test && ./build/tests/test_algoseek_source_test && ./build/tests/test_config_datasource_test`
Expected: only `Passed:` lines, exit code 0 for both.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/algoseek/algoseek_data_source.cpp src/quarkbot/algoseek/algoseek_data_source.hpp src/quarkbot/backtest/config_datasource.hpp src/tests/algoseek_source_test.cpp
git commit -m "algoseek: emit auction prints as qualified trades

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Replay CSV auction flags on trade rows

**Files:**
- Modify: `src/quarkbot/backtest/replay_csv_file.cpp:89-127`
- Modify: `src/quarkbot/backtest/replay_csv_file.hpp:20-35`
- Test: `src/tests/replay_csv.cpp`

**Interfaces:**
- Consumes: `TradeQualifier` (Task 1).
- Produces: a `trade` row's `flags` column `O`/`C`/`I`/`U` (case-insensitive) sets `Trade::qualifier`, and an empty column means `continuous`. An `auction` row with `F`/`f` throws `std::runtime_error`.

- [ ] **Step 1: Update the test**

In `src/tests/replay_csv.cpp`, replace the last CSV row:

```
1598918405348000,"TSCO.L","auction",222,45.6,111,CF
```

with:

```
1598918405348000,"TSCO.L","trade",222,45.6,"",C
```

Remove the line `CHECK(!std::get<quarkbot::Auction>(ev.data).final);` from the second auction block.

Replace the last block of `test1` (from the `CHECK(replay(ev));` before `1598918405348000` through `CHECK(std::get<quarkbot::Auction>(ev.data).final);`) with:

```cpp
    CHECK(replay(ev));
    CHECK_EQUAL(std::chrono::duration_cast<std::chrono::microseconds>(ev.time.time_since_epoch()).count(), 1598918405348000);
    CHECK_EQUAL(ev.symbol, "TSCO.L");
    CHECK(std::holds_alternative<quarkbot::Trade>(ev.data));
    CHECK_EQUAL(std::get<quarkbot::Trade>(ev.data).price , 222_dec);
    CHECK_EQUAL(std::get<quarkbot::Trade>(ev.data).size, 45.6_dec);
    CHECK(std::get<quarkbot::Trade>(ev.data).side == quarkbot::Side::undetermined);
    CHECK(std::get<quarkbot::Trade>(ev.data).qualifier == quarkbot::TradeQualifier::closing_auction);
```

In the first trade block (BTCUSD, `1598918400198000`), after `CHECK(std::get<quarkbot::Trade>(ev.data).side == quarkbot::Side::buy);`, add:

```cpp
    CHECK(std::get<quarkbot::Trade>(ev.data).qualifier == quarkbot::TradeQualifier::continuous);
```

Add two new test functions before `int main()`:

```cpp
constexpr std::string_view qualifiers_csv
= R"csv("timestamp","symbol","event","price","quantity","side","flags"
1598918400000000,"TSCO.L","trade",100,1,"",o
1598918401000000,"TSCO.L","trade",100,1,"",I
1598918402000000,"TSCO.L","trade",100,1,"",u
)csv";

void test_qualifiers() {
    quarkbot::ReplayCSVDataSource replay(qualifiers_csv);
    quarkbot::BacktestEvent ev;
    CHECK(replay(ev));
    CHECK(std::get<quarkbot::Trade>(ev.data).qualifier == quarkbot::TradeQualifier::opening_auction);
    CHECK(replay(ev));
    CHECK(std::get<quarkbot::Trade>(ev.data).qualifier == quarkbot::TradeQualifier::intraday_auction);
    CHECK(replay(ev));
    CHECK(std::get<quarkbot::Trade>(ev.data).qualifier == quarkbot::TradeQualifier::unscheduled_auction);
    CHECK(!replay(ev));
}

constexpr std::string_view final_flag_csv
= R"csv("timestamp","symbol","event","price","quantity","side","flags"
1598918405348000,"TSCO.L","auction",222,45.6,111,CF
)csv";

///old files with the final auction flag must fail loudly, not load with another meaning
void test_final_flag_rejected() {
    quarkbot::ReplayCSVDataSource replay(final_flag_csv);
    quarkbot::BacktestEvent ev;
    CHECK_EXCEPTION(std::runtime_error, replay(ev));
}
```

Replace `int main()`'s body with:

```cpp
int main() {
    test1();
    test_qualifiers();
    test_final_flag_rejected();
}
```

Add `#include <stdexcept>` to the includes.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build -j$(nproc) --target test_replay_csv && ./build/tests/test_replay_csv`
Expected: `FAILED: std::get<quarkbot::Trade>(ev.data).qualifier == quarkbot::TradeQualifier::closing_auction`.

- [ ] **Step 3: Implement**

In `src/quarkbot/backtest/replay_csv_file.cpp`, in the `trade` branch, replace:

```cpp
            else tr.side = Side::undetermined;
            tr.time = ev.time;
            ev.data = tr;
            return true;
```

with:

```cpp
            else tr.side = Side::undetermined;
            tr.time = ev.time;
            for (auto c: tmp.flags) {
                switch (c) {
                    case 'o':
                    case 'O': tr.qualifier = TradeQualifier::opening_auction;break;
                    case 'c':
                    case 'C': tr.qualifier = TradeQualifier::closing_auction;break;
                    case 'i':
                    case 'I': tr.qualifier = TradeQualifier::intraday_auction;break;
                    case 'u':
                    case 'U': tr.qualifier = TradeQualifier::unscheduled_auction;break;
                    default:break;
                }
            }
            ev.data = tr;
            return true;
```

In the `auction` branch, replace:

```cpp
                    case 'f':
                    case 'F': a.final = true;a.quantity_traded = a.quantity;break;
```

with:

```cpp
                    case 'f':
                    case 'F': throw std::runtime_error(
                        "Replay CSV: the final auction flag 'F' is no longer supported - "
                        "write the auction result as a trade row with an auction flag (O/C/I/U)");
```

Make sure `#include <stdexcept>` is among the includes of `replay_csv_file.cpp`.

In `src/quarkbot/backtest/replay_csv_file.hpp`, replace the `flags:` part of the format comment:

```
flags:  quote - empty
        trade - empty
        book - clear or empty (clear = initiate snapshot)
        auction - {O,C,I,U}, F- final (OF = opening final)
```

with:

```
flags:  quote - empty
        trade - empty = continuous trade, {O,C,I,U} = auction result
                (opening, closing, intraday, unscheduled)
        book - clear or empty (clear = initiate snapshot)
        auction - {O,C,I,U}, indicative data only. The former F (final) flag
                is rejected - write the auction result as a trade row
```

Also in the same comment, if the `price` description for `auction` reads `auction: indicative price`, keep it. Fix the typo `inticative size` → `indicative size`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build -j$(nproc) --target test_replay_csv && ./build/tests/test_replay_csv`
Expected: only `Passed:` lines, including `Passed: throw std::runtime_error`, exit code 0.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/backtest/replay_csv_file.cpp src/quarkbot/backtest/replay_csv_file.hpp src/tests/replay_csv.cpp
git commit -m "replay csv: auction results are trade rows with a flag

The F (final) flag on auction rows now throws.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: `Auction` carries indicative data only; TRTH auction trades

**Files:**
- Modify: `include/quarkbot/stream/auction.hpp`
- Modify: `src/quarkbot/trth/trth_event_source.cpp:87-149`
- Modify: `src/quarkbot/backtest/replay_csv_file.cpp` (auction branch)

**Interfaces:**
- Consumes: `to_trade_qualifier` (Task 1).
- Produces: `Auction` without `final` and `quantity_traded`, `auction_type` defaulting to `AuctionType::unknown`. TRTH emits auction trades as `Trade` with a qualifier.

- [ ] **Step 1: Remove the fields (this is the failing step: the build breaks where they are still used)**

In `include/quarkbot/stream/auction.hpp`, replace the struct with:

```cpp
    ///Indicative auction data - the auction result itself is a Trade with an auction qualifier
    struct Auction {
        struct MarketInstrumentStream {};
        ///indicative executable volume
        Decimal quantity;
        ///indicative price
        Decimal price;
        ///imbalance (surplus)
        Decimal imbalance;
        ///auction type
        AuctionType auction_type = AuctionType::unknown;
        ///timestamp
        std::chrono::system_clock::time_point time;
    };
```

- [ ] **Step 2: Verify the build fails where the fields are used**

Run: `cmake --build build -j$(nproc) 2>&1 | grep -E "error:.*(final|quantity_traded)"`
Expected: errors in `src/quarkbot/trth/trth_event_source.cpp` and `src/quarkbot/backtest/replay_csv_file.cpp` only. (Algoseek, `SimExecutor` and the tests were migrated in Tasks 3–5.) If any other file shows up, migrate it the same way before continuing.

- [ ] **Step 3: Fix replay CSV**

In `src/quarkbot/backtest/replay_csv_file.cpp`, in the `auction` branch, delete the two lines:

```cpp
            a.final = false;
            a.quantity_traded = 0;
```

- [ ] **Step 4: Fix TRTH**

In `src/quarkbot/trth/trth_event_source.cpp`, in the `"Auction"` branch, delete:

```cpp
            a.final = false;
```

and

```cpp
            a.quantity_traded = 0;
```

In the `"Trade"` branch, replace:

```cpp
            if (atype == quarkbot::AuctionType::unknown) {
                auto &t = ev.data.emplace<Trade>();
                t.price = Decimal::from_string(_data.Price);
                t.size = _data.Volume.empty()?0_dec:Decimal::from_string(_data.Volume);
                t.time = ev.time;
            } else {
                auto &a = ev.data.emplace<Auction>();
                a.auction_type = atype;
                a.final = true;
                a.quantity = a.quantity_traded =  _data.Volume.empty()?0_dec:Decimal::from_string(_data.Volume);
                a.imbalance = 0;
                a.price = Decimal::from_string(_data.Price);
                a.time = ev.time;
            }        
```

with:

```cpp
            auto &t = ev.data.emplace<Trade>();
            t.price = Decimal::from_string(_data.Price);
            t.size = _data.Volume.empty()?0_dec:Decimal::from_string(_data.Volume);
            t.time = ev.time;
            //an auction uncross is reported as a trade with the auction's MMT trading mode
            t.qualifier = to_trade_qualifier(atype);
```

- [ ] **Step 5: Build and run the full suite**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: the build succeeds with no errors, and every test passes. That includes `tests/trade_qualifier`, `tests/auction_phase_tracker_test`, `tests/sim_auction_test`, `tests/algoseek_source_test` and `tests/replay_csv`.

Run: `grep -rn "\.final\b\|quantity_traded" src include`
Expected: no matches referring to `Auction` (the WebSocket parser's `final` frame flag in `src/quarkbot/network/ws_parser.hpp` is unrelated and stays).

- [ ] **Step 6: Commit**

```bash
git add include/quarkbot/stream/auction.hpp src/quarkbot/trth/trth_event_source.cpp src/quarkbot/backtest/replay_csv_file.cpp
git commit -m "stream: Auction carries indicative data only

Drop Auction::final and quantity_traded - the auction result is a Trade
with an auction qualifier. TRTH emits MMT auction trades that way.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
