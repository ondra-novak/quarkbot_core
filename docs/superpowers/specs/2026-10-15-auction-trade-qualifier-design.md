# Auction trades as qualified `Trade` events

Date: 2026-10-15

## Background

Today an auction result travels on the `Auction` stream as an event with
`final == true` (and `quantity_traded`). The same stream also carries indicative
auction data (indicative price, executable volume, imbalance) with
`final == false`. `SimExecutor` fills auction orders (`ato` / `atc` /
`crossing`) only when it sees a final `Auction`, and it ignores all other
orders during an auction.

Applying the library to backtests over real data and to paper trading over live
market feeds (separate projects) showed that this model differs from how
venues and data vendors work:

- Feeds and vendors report the auction uncross as a **trade** with a qualifier.
  Under MiFID II this is the FIX Trading Community *Market Model Typology*
  (MMT) Level 2 "trading mode". FIX's `TradingSessionSubID (625)` carries the
  same idea. TRTH's `MMT_CLASS`, already parsed in `trth_event_source.cpp`, uses
  exactly these values.
- On a venue (Xetra T7 market model is the reference here), **regular** resting
  orders take part in scheduled auctions. Every participating order executes at
  the single auction price, not at its own limit. A stop order is triggered by
  an auction price with turnover. Orders restricted to auctions (`ato`, `atc`,
  `crossing`) take part in the matching auction only.
- Some sources carry no indicative data at all (Algoseek, IQFeed). Some report
  one auction as several prints, for example IQFeed, which streams US equities
  from multiple market centers.

The examples in this repository do not use auctions (only tests do), so the
discrepancy did not show up here earlier.

## Goals and non-goals

**Goals**

- Carry the auction result as a `Trade` event with a phase qualifier.
- Restrict the `Auction` stream to indicative data (price, volume, imbalance).
- Make `SimExecutor` simulate auctions realistically: a call phase without
  continuous matching, a single-price uncross for auction and regular orders,
  and stop triggering by the auction price.
- Work both with indicative data (phase driven by data) and without it, when
  only the auction trade is available.
- Update the data sources that can detect auction trades: Algoseek, TRTH and
  replay CSV.

**Non-goals**

- Incomplete data, for example a missing auction trade together with missing
  indicative data. That is postponed to a later phase.
- Validating that input data is clean. See *Data assumptions*.
- Identifying market centers to choose the "right" print among duplicates.
- A general-purpose trade condition set (FIX `TradeCondition (277)` style).
- Configurable auction schedules per instrument.

## Data assumptions

Auction evaluation is correct only on prepared data:

- no pre-market and no after-market trades,
- no trades during an auction call phase (OTC noise and similar).

Whoever prepares the data is responsible for this, and the simulator does not
check it. The simulator must still work when indicative data is absent and only
the auction trade exists. Such a trade is taken as a real auction result.

## Data model (`include/quarkbot/stream/`)

### `trade.hpp`

```cpp
enum class TradeQualifier : char {
    continuous          = 0,     ///< continuous trading phase (default)
    opening_auction     = 'O',
    closing_auction     = 'C',
    intraday_auction    = 'I',
    unscheduled_auction = 'U',   ///< e.g. reopening after a halt
};

struct Trade {
    struct MarketInstrumentStream {};
    Decimal price;
    Decimal size;
    std::chrono::system_clock::time_point time;
    Side side = Side::undetermined;
    ///phase the trade was executed in
    TradeQualifier qualifier = TradeQualifier::continuous;

    bool operator==(const Trade &) const = default;
};

constexpr bool is_auction_trade(TradeQualifier q);            // q != continuous
constexpr AuctionType to_auction_type(TradeQualifier q);      // continuous -> unknown
constexpr TradeQualifier to_trade_qualifier(AuctionType t);   // unknown -> continuous
```

- The enum values reuse the `AuctionType` characters (O/C/I/U) and match MMT
  Level 2.
- The new field goes last and has a default, so existing aggregate
  initialization (`Trade{price, size, time, side}` in `minute_data_source.cpp`
  and in tests) still compiles, and every source without auctions keeps
  producing `continuous`.
- `trade.hpp` includes `auction.hpp` for the conversion helpers.

### `auction.hpp`

```cpp
struct Auction {
    struct MarketInstrumentStream {};
    Decimal quantity;      ///< indicative executable volume
    Decimal price;         ///< indicative price
    Decimal imbalance;     ///< imbalance (surplus)
    AuctionType auction_type = AuctionType::unknown;
    std::chrono::system_clock::time_point time;
};
```

`final` and `quantity_traded` are removed. `auction_type` gets a default value,
because today it is left uninitialized.

### Effect on `Trade` stream consumers

Auction trades now reach everything built on `Trade`: closed and ranged bars
(`closed_bar_lambda`, `ranged_bar_lambda`), `TradeStat` and stop triggers in
`order_trigger`. This is intended, because venues count the auction price and
volume in OHLC and volume statistics. A strategy that wants continuous trades
only filters on `qualifier`.

## `AuctionPhaseTracker` (`src/quarkbot/backtest/auction_phase_tracker.{hpp,cpp}`)

`AuctionPhaseTracker` is a per-instrument state machine with no knowledge of
orders. It takes events and returns actions that `SimExecutor` carries out. It
is keyed by instrument name, like the current `_auction_state` and for the
same reason: `SimInstrument` instances are cached through a `weak_ptr` and can
be recreated. It replaces `SimExecutor::AuctionState`.

### State per instrument

- `seen_auction`: the instrument has had at least one auction event, indicative
  or trade. Until then, auction orders are rejected with `not_tradable`.
- `phase`: one of `continuous`, `call(T)` or `closed`.
- `opening_done`: today's opening auction is over, whether it happened or not.
  After it, `ato` is rejected with `too_late`.
- `close_timeout_started`: the 30-minute closing timeout is already running
  today.
- `last_uncross[T]`: time of the last uncross per auction type. It is used for
  duplicate detection of intraday and unscheduled auctions.

### Transitions

| Event | Condition | Transition | Actions for the executor |
|---|---|---|---|
| indicative `Auction(T)` | T = unknown | none | none |
| indicative `Auction(opening)` | phase `closed` | new day (`opening_done = false`, `close_timeout_started = false`), → `call(opening)` | none |
| indicative `Auction(T)` | otherwise | → `call(T)` | for T = closing, the first time today: start the closing timeout |
| auction `Trade(T)` | not a duplicate (see below) | opening → `continuous` + `opening_done`; closing → `closed`; intraday/unscheduled → `continuous`. An opening trade in `closed` first starts a new day. | `uncross(T)`, then `finish(T)`, and for closing also `close_day` |
| auction `Trade(T)` | duplicate | handled as a continuous trade (rows below) | as for a continuous trade |
| continuous `Trade` | phase `call(opening / intraday / unscheduled)` | the auction did not happen → `continuous`; for opening set `opening_done` | `finish(T)` (expire the auction orders for T) |
| continuous `Trade` | phase `call(closing)` | none; stays in the call phase (cannot happen on clean data) | none |
| continuous `Trade` | phase `closed` | new day → `continuous`, `opening_done = true` (the opening auction was missing) | none |
| continuous `Trade` | phase `continuous` | none | match the trade (`can_match_continuous`) |
| closing timeout fires | phase is not `closed` | → `closed` | `finish(closing)` + `close_day` |

The closing-auction duplicate (last row of *Duplicate detection*) is an
exception: it is checked before the `closed` row above and never starts a new
day.

### Duplicate detection

One auction may arrive as several prints. The first print runs the auction,
and every following print of the same type is a duplicate that the state
machine handles as a continuous trade.

- **opening:** an `opening_auction` print after `opening_done` is a duplicate.
  The check runs after the new-day reset, so the first opening print following
  `closed` is the next day's auction, not a duplicate.
- **closing:** a `closing_auction` print while `closed` is a duplicate. It does
  not reopen the day, and because nothing matches in `closed`, it has no effect
  on orders.
- **intraday / unscheduled:** a print of the same type within
  `duplicate_window` (constant, 2 minutes) after the last uncross of that type
  is a duplicate. A later print is a new auction. Real intraday auctions follow
  halts with pauses of about 5 minutes, while multi-venue duplicates arrive
  within seconds.

### Queries

- `can_match_continuous(name)` is `true` only in phase `continuous`, and also
  for an instrument the tracker has never seen. Outside it, regular orders are
  matched neither against quotes nor against trades.
- `check_auction_order(name, tif)` returns either a rejection reason or
  "rest". It takes over today's validation from `match_order(Quote)`:
  - `not_tradable` when `!seen_auction`,
  - `too_late` when `closed`,
  - `too_late` when `tif == ato && opening_done`.
- `is_closed(name)` is used by the rule that rejects a `day` order with
  `too_late`.

## `SimExecutor` behaviour

### Uncross of auction type T at price P, first-print size S

Every active order of the instrument is handled as follows:

| Order | Takes part | Fill |
|---|---|---|
| auction TIF valid for T (`is_valid_order_for_auction_type`) | yes | by order type, as below |
| auction TIF not valid for T | no | none |
| regular **market** (`day` / `gtc`), including a triggered stop | yes | the full remaining quantity at P; no volume cap |
| regular **limit** crossing P (buy ≥ P, sell ≤ P), including a triggered stop limit | yes | `min(remaining, S)` at P, not at its own limit |
| limit not crossing P | no | none |
| **untriggered** stop / stop limit / alert | no | P is compared with the stop price, and if reached the order is triggered (`trig`; an alert goes to filled). It does not join this uncross; it is matched in the continuous phase. |

- An auction market order with an auction TIF follows the market row, and an
  auction limit order follows the limit rows.
- Fees are computed as taker, as today.
- The volume cap applies only to limit orders, as a rough model of partial
  execution. The simulator is not a market participant, so a large order would
  have changed the real outcome anyway.
- The cap is the size of the **first** print only. When the first print is a
  small print from a secondary market center, auction orders are under-filled.
  This simplification is known and accepted.

### `finish(T)`

- An auction-TIF order valid for T that is fully filled becomes `filled`. Any
  unfilled remainder (including zero fill) becomes `expired`.
- Regular orders stay active, possibly partially filled.
- For closing, `close_day` follows. It is unchanged: it expires everything
  except `gtc`.

### Other events

- **`on_event(Quote)`:** `_last_quote` is always updated, and matching runs only
  when `can_match_continuous`. Our resting limits are still reflected in the
  published quote, as today.
- **`on_event(Trade)`:** the event goes to the tracker first, and its actions
  run (uncross, finish, close_day). If the trade is continuous, or an auction
  duplicate, today's `match_order(order, trade)` then runs, but only when
  `can_match_continuous`.
- **`on_event(Auction)`:** the event goes to the tracker, which may start the
  closing timeout (`expire_auction`). It produces no fills.
- **`expire_auction` (timer):** calls the tracker's timeout, and on the
  transition to `closed` runs `finish(closing)` and `close_day`.

### Placing an order

- **Auction TIF:** the order passes through `check_auction_order` and is either
  rejected or rests. It is never matched immediately.
- **Regular order:**
  - a `day` order while `is_closed` is rejected with `too_late`. This is today's
    rule, moved from `match_order(Quote)` to placement,
  - when `can_match_continuous`, the order is matched immediately, as today,
  - otherwise it rests, and `ioc` / `fok` are canceled through
    `finish_if_cannot_rest` (`not_filled`).
- **Replace** follows the same rules.

## Data sources

### Algoseek (`algoseek_data_source.cpp`)

Opening, closing and reopening prints become `Trade` with `opening_auction`,
`closing_auction` and `unscheduled_auction`. Price, size and time come from the
row, and the side is `undetermined`. The `auctions` counter stays and counts
auction trades. Fake quotes are generated for continuous trades only, as today.
Documentation in `algoseek_data_source.hpp` and `config_datasource.hpp` is
updated: there are no `Auction` events any more.

### TRTH (`trth_event_source.cpp`)

A trade whose `MMT_CLASS` level 2 is O/C/U/I becomes `Trade` with the matching
qualifier. Indicative `Auction` events stay, without `final` and
`quantity_traded`.

### Replay CSV (`replay_csv_file.{hpp,cpp}`)

- For `trade` rows, the `flags` column (empty today) takes O/C/I/U
  (case-insensitive) as the qualifier, and an empty column means `continuous`.
- An `auction` row with flag `F` throws `std::runtime_error`, with a message
  that final auctions are now `trade` rows with an auction flag. Old files
  therefore cannot be read silently with the wrong meaning.
- The format comment in the header is updated.

### `SimExchange`

No change. It forwards `Trade` and `Auction` to the executor and the streams,
as today.

## Testing

- **`algoseek_source_test.cpp`:** auction prints are checked as `Trade` with
  the right qualifier, and the aggregate auction counts still match.
- **`replay_csv.cpp`:** the `CF` row becomes a `trade` row with flag `C`, which
  is checked as `Trade` + `closing_auction`. A new case checks that an
  `auction` row with `F` throws.
- **New `auction_phase_tracker_test.cpp`** (pure state machine): every
  transition in the table, duplicates for opening and closing, the intraday
  time window, a new day after `closed`, a missing opening auction, the closing
  timeout, and `check_auction_order` results.
- **New `sim_auction_test.cpp`** (full `SimExecutor` through `SimExchange`):
  - `ato` / `atc` / `crossing` fills and expirations,
  - a regular limit filled at the auction price,
  - a market order filled without the volume cap,
  - a limit capped by the first print,
  - a stop triggered but not filled in the uncross,
  - no matching during the call phase, and IOC canceled during it,
  - duplicate prints not filling twice through the auction path,
  - `close_day` after the closing auction and after the timeout,
  - the flow without indicative data (Algoseek / IQFeed style).
- Both new tests are added to `BASIC_TESTS` in `src/tests/CMakeLists.txt`.
