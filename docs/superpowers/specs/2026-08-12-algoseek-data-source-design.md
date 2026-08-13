# Algoseek data source: trade replay from Algoseek US equity CSV exports

Date: 2026-08-12

## Background

`BacktestDataSource` (`include/quarkbot/abstract/backtest_data_source.hpp`) is a
`std::move_only_function<bool(BacktestEvent &)>` — called repeatedly, it fills an
event and returns `false` at end of data. Existing implementations live in
`src/quarkbot/`: `ReplayCSVDataSource` (native gzip CSV format),
`TardisTradesDataSource` / `TardisQuotesDataSource` (crypto exports) and
`TRTHEventSource` (Refinitiv/LSEG tick history).

Algoseek is a US equity market data vendor. Their "Trades Only" export is one
gzip CSV file per ticker per day with a fixed eight-column layout:

```
Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange,Conditions
20230609,09:30:00.791480832,TRADE NB,BIPC,47.58,4000,NYSE,20000040
```

We have Algoseek trade data but no quote data, so this source produces `Trade`
and `Auction` events only. A companion Java project
(`/home/ondra/vscode/AlgoseekUtils`) already reads this format for a different
purpose (auction history extraction) and is the reference for field semantics.

## Goals and non-goals

**Goals**

- Replay Algoseek trades as `Trade` events on the simulation timeline.
- Emit auction prints as final `Auction` events.
- Restrict a replay to a single exchange, so venues are never mixed.
- Convert the file's local wall-clock timestamps to UTC.
- Wire the source into the `[data-source]` INI configuration.

**Non-goals**

- Quote, order book, or ticker events — the data does not contain them.
- Indicative (non-final) auction events. Algoseek publishes no auction
  imbalance or indicative price feed, so `final` is always `true` and the
  unavailable fields take default values.
- Retracting already-replayed trades on `TRADE CANCELLED` (see
  "Cancellations" below).
- Algoseek S3 download / fetching. The source consumes local files.

## Findings from the reference data

The Java project ships ten real export files as test resources (~900 000 rows
total). These were measured rather than assumed, and several design decisions
follow directly from the measurements.

| Property | Measurement |
|---|---|
| Column count / decimal separator | Always exactly 8 fields, `.` as decimal separator, up to 4 decimal places |
| Row ordering | 0 monotonicity violations across all files |
| Ticker / date per file | Exactly one of each |
| `Timestamp` format | Always `HH:MM:SS.` + 9 digits |
| `Conditions` format | Always 8 lowercase hex digits |
| Timezone | `America/New_York` (`AlgoseekConsts.DATA_FILES_TIMEZONE`) |

**`EventType` values** are `TRADE`, `TRADE NB` (note the space) and
`TRADE CANCELLED`.

**`TRADE` and `TRADE NB` are disjoint sets of real trades**, not duplicates of
each other: of 27 253 `TRADE NB` rows in one file, only 5 coincidentally matched
a nearby `TRADE` on price/quantity/exchange. Both must be emitted.

**`Conditions` is a 32-bit flag bitmask**, decoded per
`AlgoseekTradeFlag`: bit 6 `tOpeningPrints`, bit 7 `tClosingPrints`, bit 8
`tReOpeningPrints`, bit 13 `tExtendedHours`, bit 24 `tOfficialClose`, bit 26
`tOfficialOpen`, bit 31 `tOddLot`, and others. Bit 30 is undefined in the Java
enum but does occur in the data.

**Auctions are identified by flag, not by `EventType`.** Each file contains
exactly one `tOpeningPrints` row and one `tClosingPrints` row (occasionally one
`tReOpeningPrints`), and these appear on `TRADE` in some files and on
`TRADE NB` in others.

**`tOfficialOpen` / `tOfficialClose` rows are administrative re-broadcasts, not
trades.** In `20230609_BIPC.csv.gz` the closing print `47.92 × 23455` on NYSE is
repeated as `tOfficialClose` four times — at 16:00:02, 16:10:00, 18:30:00 and
19:00:00. Emitting those as trades would inject four times the closing auction
volume into the replay, including a phantom print at 19:00. Each venue also
publishes its own official open as a re-tag of an earlier trade. These rows must
be dropped.

**`Quantity = 0` rows are administrative too.** All 18 occurrences are on NYSE
with `Conditions = 0x60000000`, at 16:10, 18:30 and 19:00 — end-of-day closing
price re-broadcasts.

**Off-market prints are concentrated on `Exchange = FINRA`** (the TRF/OTC
reporting facility, not an exchange). Measured deviation from the preceding
regular trade price:

| Flag | Count | Median deviation | Venues |
|---|---|---|---|
| `tDerivativelyPriced` | 24 811 | 3.0 bps | FINRA 100 % |
| `tAveragePrice` | 3 900 | 4.8 bps | FINRA 100 % |
| `tPriorReferencePrice` | 142 | 1.6 bps | FINRA 100 % |
| `tCash`/`tNextDay`/`tSeller`/`tStockOption` | 321 | — | FINRA ≈100 % |
| `tOutOfSequence` | 16 | 129 bps | FINRA 15, BATS 1 |
| `tFormT` | 24 151 | 29.5 bps | ARCA, NASDAQ, EDGX, FINRA |
| `tPriceVariation` | 25 | 181 bps (max 1069) | NYSE |

Consequence: **the exchange filter already removes the non-tradable prints**, so
no flag-based skip list is needed. That information lives in the `Exchange`
column, and suppressing it would be our policy rather than the data's. The
residue on real venues is `tFormT` (genuine extended-hours trades — the data
starts at 04:00 ET) and 25 `tPriceVariation` rows on NYSE, which are real trades
at unusual prices.

**Cancellations cannot be resolved by look-ahead.** Measuring the row distance
from each `TRADE CANCELLED` back to the trade it cancels: some are 1–2 rows
away, but many are 35 000+ rows away — end-of-day bulk cancellations of morning
trades. A bounded look-ahead window would therefore be incorrect for a large
fraction of cases, and unbounded look-ahead means buffering the whole day.

## Configuration syntax

A data source entry is a file path with an optional URL-style query string:

```ini
[data-source]
algoseek=IBM.csv.gz?exchange=NASDAQ&tzone=America/New_York
```

The path is everything before the first `?`; it is resolved relative to the
directory of the INI file, as with the other keys. Parameters are `&`-separated
`key=value` pairs.

| Parameter | Required | Default | Meaning |
|---|---|---|---|
| `exchange` | no | no filter | Emit only rows whose `Exchange` equals this value |
| `tzone` | no | `UTC` | IANA zone name of the file's wall-clock timestamps |
| `symbol` | no | `Ticker` column | Symbol reported on emitted events |

An unknown parameter key is an error, not something to ignore — a typo in a
config is worse silent than loud.

`exchange` is compared verbatim and case-sensitively against the `Exchange`
column. Algoseek uses values such as `NASDAQ`, `NYSE`, `ARCA`, `EDGX`, `IEX`,
`FINRA`, and also some containing a space (`BATS Y`, `MIAX PEARL`,
`NASDAQ BX`); those are written literally, with no URL-style escaping. A typo
here cannot be detected by comparison — an unrecognised venue name simply
matches nothing — so the EOF summary warns when a source produced no events at
all.

`tzone` defaults to UTC for predictability in tests. Real Algoseek exports are
in `America/New_York` and must say so, otherwise every timestamp is off by four
or five hours.

`symbol` exists to resolve a collision. Two entries such as
`IBM.csv.gz?exchange=NASDAQ` and `IBM.csv.gz?exchange=ARCA` would both report
symbol `IBM`, and `MergedDataSource` would fold them into a single instrument —
which is exactly the venue mixing the exchange filter is meant to prevent. The
`[symbol-mapping]` section cannot help, because it keys on the source symbol,
which is identical for both. `symbol=IBM.ARCA` states the distinction
explicitly. Note that `FINRA` as an `exchange` value selects the OTC/TRF prints
described above.

## Architecture

A new component directory `src/quarkbot/algoseek/` contributing sources to
`quarkbot_impl`, following the `tardis` and `trth` pattern. No new CMake option:
`find_package(ZLIB REQUIRED)` is already unconditional at the top level, and
zlib is the only external dependency. The public include path becomes
`<quarkbot/algoseek/algoseek_data_source.hpp>`.

Three units, split along testability lines:

| Unit | Responsibility | Dependencies |
|---|---|---|
| `algoseek_spec.hpp` / `.cpp` | Parse `<file>?exchange=…&tzone=…&symbol=…` | none — pure function, no I/O |
| `local_time_converter.hpp` | Local wall clock → UTC with cached `sys_info` | `<chrono>` only |
| `algoseek_data_source.hpp` / `.cpp` | gzip CSV reading, filtering, row → event | zlib, `CSVReader`, the two above |

The timezone converter is a separate unit because it holds the only non-trivial
logic with edge cases (DST transitions), and separating it means those cases can
be tested without a file on disk.

### `AlgoseekSpec`

```cpp
struct AlgoseekSpec {
    std::filesystem::path file;
    std::string exchange;                // empty = no filter
    const std::chrono::time_zone *tz;    // defaults to locate_zone("UTC")
    std::string symbol;                  // empty = use the Ticker column
};

AlgoseekSpec parse_algoseek_spec(std::string_view spec);
```

An unknown key, a parameter without `=`, or an unknown zone name throws
`std::runtime_error` carrying the offending key and the full spec string. The
returned `file` is relative; the caller joins it with the config directory.

### `LocalTimeConverter`

Holds a `const std::chrono::time_zone *` and a cached `std::chrono::sys_info`
(UTC offset plus the `begin`/`end` of the interval over which it applies).

Conversion of a local time `lt`: compute `candidate = lt - cached.offset`; if
`candidate` falls in `[cached.begin, cached.end)` the cached offset was correct
and no lookup happens. Otherwise refresh via `tz->get_info(lt)` and retry, using
the earlier of the two candidate offsets for ambiguous local times.

For a single-day file this is one timezone lookup per file rather than one per
row. For UTC it degenerates to a zero offset over an unbounded interval.

### `AlgoseekDataSource`

Constructed from an `AlgoseekSpec`. Reads the gzip stream in 64 KB blocks into a
`CSVReader<CSVSource>` with a `CSVFieldIndexMapping<Data>`, mirroring
`TRTHRawSource`. The raw-reader / event-mapper split that `trth` uses is not
reproduced here: that split exists because two importers share the raw reader,
and there is one consumer here.

`readRow` handles malformed field counts safely — surplus fields are consumed
and discarded, missing fields are cleared rather than left stale — so a short
row yields empty strings instead of data from the previous row.

Per-row pipeline, each step with its own counter:

```
1. EventType == "TRADE CANCELLED"        → skip (++cancelled)
2. EventType ∉ {TRADE, TRADE NB}         → skip (++unknown_event, warn once)
3. exchange filter set && Exchange ≠ it  → skip (++filtered_exchange)
4. Quantity <= 0                         → skip (++zero_qty)
5. Price <= 0                            → skip (++zero_price)
6. flags = parse hex Conditions
7. bit 6 → Auction{opening}      ┐  final = true
   bit 7 → Auction{closing}      ├  quantity = quantity_traded = Quantity
   bit 8 → Auction{unscheduled}  ┘  price = Price, imbalance = 0
8. bit 24 (tOfficialClose) || bit 26 (tOfficialOpen) → skip (++official_print)
9. otherwise → Trade{price = Price, size = Quantity, side = undetermined}
```

`Date` is parsed as `%Y%m%d` and `Timestamp` as `HH:MM:SS` plus a nine-digit
fraction; the two combine into a `local_time<nanoseconds>` that
`LocalTimeConverter` turns into UTC.

`ev.symbol` is the `symbol` override when set, otherwise the `Ticker` column.
`ev.time` is the converted UTC timestamp, and the `Trade`/`Auction` `time` field
carries the same value.

Step 7 deliberately precedes step 8: if a row ever carried both an auction print
flag and an official-price flag, the auction reading must win. In the observed
data the two are disjoint (`20200081` versus `01000001`), but the ordering does
not depend on that holding.

`tReOpeningPrints` maps to `AuctionType::unscheduled` — a reopening auction
follows a trading halt, as opposed to `intraday`, which denotes a scheduled
midday auction.

`Side` is always `Side::undetermined`; Algoseek trade rows carry no aggressor
side.

## Data flow

```
[data-source]
algoseek=IBM.csv.gz?exchange=NASDAQ&tzone=America/New_York
    │
    ▼
configure_datasources()  (src/quarkbot/backtest/config_datasource.cpp)
    └── add_algoseek(row.value)
            ├── parse_algoseek_spec(value)     ← split the query off first
            ├── spec.file = root / spec.file   ← only then join with config dir
            └── sources.push_back(AlgoseekDataSource(spec))
    │
    ▼
MergedDataSource → SymbologyMapping → BacktestDataSource
```

`SourceCollector::walk` currently joins the path before dispatch
(`add_tardis(root/row.value)`). That cannot work for this key, because
`root/"IBM.csv.gz?exchange=NASDAQ"` would bake the query into the path. The
`algoseek` branch passes the raw value and joins after parsing.

The doc comment listing the recognised keys in `config_datasource.hpp` gains the
`algoseek` entry. Note that `IniReader` only treats a line as a comment when it
*starts* with `;` or `#`, so a trailing comment after a value becomes part of the
value — the documentation examples must not use inline comments.

## Error handling

Configuration and file-structure errors fail at construction, so a broken setup
surfaces at startup rather than twenty minutes into a backtest. Nothing is
dropped silently: every discarded row increments a counter, and the counters are
logged at EOF.

**At construction**

| Condition | Reaction |
|---|---|
| Unknown query parameter | `runtime_error` with the key and the spec string |
| Parameter without `=` | `runtime_error` |
| Unknown `tzone` | `runtime_error` wrapping the `locate_zone` failure |
| `gzopen` fails | `runtime_error` with the path |
| Required header column missing | `runtime_error` listing the missing columns (via `colmap.isMapped`) |

All eight columns are required.

**Per row** — an invalid row throws with its row number rather than being
skipped:

| Condition | Reaction |
|---|---|
| Empty required field | `runtime_error` with row number and column name |
| `Conditions` not 8 hex digits | `runtime_error` |
| Unparseable `Date` / `Timestamp` | `runtime_error` |
| Timestamp decreases | `runtime_error` with both timestamps |
| `Ticker` changes mid-file | `runtime_error` |

This is stricter than `TardisTradesDataSource`, which does `catch (...) continue`.
For a trade replay, silently skipping malformed rows is worse than failing:
prices and volumes drift and the backtest believes the result. The reference data
is clean across 900 000 rows, so a bad row means a bad file.

The `Conditions` check doubles as the guard against shifted columns, for the
*unquoted* comma-decimal variant: a file whose `Price` was rewritten with a comma
decimal separator (as happens when an export passes through a spreadsheet in a
European locale) has nine fields per row, which shifts `Conditions` to `EDGX` —
not hex, so it fails loudly on the first row. A spreadsheet that instead quotes
the field (`…,BIPC,"47,58",4000,NYSE,20000040`) keeps eight fields, so this check
does not fire; that variant is caught by the numeric parse of `Price` instead,
which also fails loudly and names the row and column.

Monotonicity is enforced because `MergedDataSource` is a k-way merge over source
heads; unordered input would corrupt the merged timeline without any other
symptom. The `Ticker` check prevents a `symbol` override from folding two
different tickers into one instrument.

**Known limitation:** a file whose local timestamps cross the autumn DST
transition (01:00–02:00 in a US zone) would have non-decreasing local times but
decreasing UTC times, and would trip the monotonicity check. Algoseek trading
data spans 04:00–20:00 ET, so this cannot arise in practice.

**EOF summary** via `logInfo`, or `logWarning` when `unknown_event > 0` or
`trades + auctions == 0`. Counters: `trades`, `auctions`, `cancelled`,
`unknown_event`, `filtered_exchange`, `zero_qty`, `zero_price`,
`official_print`.

The zero-event warning is what catches a misspelled `exchange` value, which is
otherwise indistinguishable from a venue that genuinely never traded.

Cancellations are routine in real exports — 6 of the 9 reference files contain
one (1, 75, 34, 2, 7 and 5 occurrences) — so warning whenever `cancelled > 0`
would fire on most healthy files and drown out the two conditions above that
actually indicate a problem. The `cancelled` counter is still reported in every
summary line, just at whichever level the other counters set.

## Testing

`src/tests/algoseek_source_test.cpp`, added to the `BASIC_TESTS` list in
`src/tests/CMakeLists.txt`, producing `test_algoseek_source_test` and the ctest
entry `tests/algoseek_source_test`. Assertions use `src/tests/check.h`;
synthetic gzip inputs are written with `gzopen`/`gzwrite` as in
`tardis_source_test.cpp`.

**A. Spec parsing** (no I/O): bare path yields no filter, UTC and no symbol
override; a fully specified query yields each value; unknown key, unknown zone
and a parameter without `=` each throw; empty query after `?`.

**B. `LocalTimeConverter`**: EST (−05:00) and EDT (−04:00);
`20260409 04:00:00.010833553` ET → `2026-04-09T08:00:00.010833553Z` (verified
against the system tzdb); two conversions straddling a DST transition on one
instance, exercising cache invalidation; UTC as identity; nanosecond precision
preserved.

**C. Row → event mapping** (synthetic CSV): `TRADE` yields a `Trade`; `TRADE NB`
also yields a `Trade`; bits 6/7/8 yield `Auction{opening|closing|unscheduled}`
with `final == true`, `quantity == quantity_traded`, `imbalance == 0`; bits 24
and 26 are dropped; `Quantity = 0` is dropped; `TRADE CANCELLED` is dropped and
the preceding `Trade` survives; exchange filter on and off; `symbol` override
applied; a row with both bit 7 and bit 24 yields an `Auction`, covering the
step 7 / step 8 precedence.

**D. Error cases**: missing header column; a nine-field row; non-hex
`Conditions`; decreasing timestamp; `Ticker` change; nonexistent file.

**E. Regression against real data.** Two small fixtures copied from the Java
project into `src/tests/data/`, with the directory passed to the test through
`target_compile_definitions` (following the `TEST_PLUGIN_PATH` pattern).

`20240418_NASDAQ_DHIL.csv.gz` — 7 KB, 594 rows:

| | no filter | `exchange=NASDAQ` |
|---|---|---|
| events | 590 | 271 |
| trades | 588 | 269 |
| auctions | 1 opening + 1 closing | 1 opening + 1 closing |
| `official_print` | 4 | 2 |
| `filtered_exchange` | 0 | 321 |

`20230609_BIPC.csv.gz` — 59 KB, 5 627 rows; this is the file containing the
four-times-repeated official close and three `Quantity = 0` rows:

| | no filter | `exchange=NYSE` |
|---|---|---|
| events | 5 615 | 882 |
| trades | 5 613 | 880 |
| auctions | 1 opening + 1 closing | 1 opening + 1 closing |
| `official_print` | 9 | 5 |
| `zero_qty` | 3 | 3 |
| `filtered_exchange` | 0 | 4 737 |

Specific auction values to assert (2023-06-09 is EDT):

- opening: `09:30:00.791480832` ET → `13:30:00.791480832Z`, 47.58 × 4000
- closing: `16:00:02.164273920` ET → `20:00:02.164273920Z`, 47.92 × 23455

Together the two fixtures cover, on real data, what would be easiest to break:
duplicate official prints, zero quantities, the exchange filter and the timezone
conversion. Cancellations are covered synthetically, since the small fixtures
contain none.

## Notes for implementation

The gzip-block-reader plus `CSVReader` boilerplate will be the third copy in the
tree (`trth_raw_source.cpp` and `replay_csv_file.cpp` hold the other two). It is
duplicated again here rather than extracted, because extracting it would mean
changing two working importers that are otherwise outside the scope of this
work. Worth revisiting if a fourth appears.
