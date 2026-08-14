# Tardis data source: trade and quote replay from Tardis.dev CSV datasets

Date: 2026-08-13

## Background

`BacktestDataSource` (`include/quarkbot/abstract/backtest_data_source.hpp`) is a
`std::move_only_function<bool(BacktestEvent &)>` — called repeatedly, it fills an
event and returns `false` at end of data. `configure_datasources()`
(`src/quarkbot/backtest/config_datasource.cpp`) builds one from an INI file,
merging every configured source into a single timeline.

`TardisTradesDataSource` and `TardisQuotesDataSource` have existed in
`src/quarkbot/tardis/` since before this design, with a passing unit test, but
they were never reachable from a configuration: `add_tardis()` throws
`"Tardis support is not yet implemented"`. The reason was the shape of the
vendor's data rather than the shape of the reader. The replay format was designed
around a self-contained file that carries everything needed to replay it, and
Tardis splits one instrument across several files by data type — trades in one,
quotes in another — which the single-file-per-source key could not express.

The `algoseek.*` option keys added in `e1f24a9` changed that. A source may now be
configured by several keys of the `[data-source]` section, collected per
configuration file. That mechanism is what this design applies to Tardis.

Three real exports downloaded from tardis.dev sit in `data/` and are the
reference for every claim about the file format below. They are 113 MB together
and are not committed; see Testing.

## Goals and non-goals

**Goals**

- Replay Tardis `trades` and `quotes` CSV exports from an INI configuration.
- Merge the data types of one instrument into one timeline automatically, with no
  file pairing written in the configuration.
- Read the format Tardis actually publishes, verified against real exports.
- Fail at startup on anything detectable at startup.

**Non-goals**

- `book_snapshot_*` and `incremental_book_L2`. The event types
  (`OrderBookSnapshot`, `OrderBookIncrement`) already exist in `BacktestEvent`,
  so these become new `tardis.*` keys later without changing the scheme.
- Replacing the hand-rolled CSV splitting with the in-tree `CSVReader`. See
  Deferred work.
- Live Tardis API access. This reads downloaded files only.

## What the real exports look like

All three files share the first four columns and differ only in the payload:

```
bitmex_trades_2020-04-01_XBTUSD.csv.gz
exchange,symbol,timestamp,local_timestamp,id,side,price,amount
bitmex,XBTUSD,1585699202957000,1585699203089980,d202810a-…,buy,6425.5,12

huobi-dm-swap_quotes_2020-04-01_BTC-USD.csv.gz
exchange,symbol,timestamp,local_timestamp,ask_amount,ask_price,bid_price,bid_amount
huobi-dm-swap,BTC-USD,1585699201147000,1585699201270777,86,6423,6422.9,112

binance-futures_book_ticker_2024-04-01_ETHUSDT.csv.gz
exchange,symbol,timestamp,local_timestamp,ask_amount,ask_price,bid_price,bid_amount
binance-futures,ETHUSDT,1711929600008000,1711929600011559,17.832,3648.8,3648.79,46.606
```

Four facts follow, each of which contradicts the existing reader.

**Timestamps are microseconds.** `1711929600008000` is 16 digits and resolves to
2024-04-01. `parse_ns_timestamp()` reads the column as nanoseconds, which places
every real event roughly a thousand times too far from the epoch.

**Column names are `snake_case`.** `TardisQuotesDataSource` looks for
`bidPrice`, `bidSize`, `askPrice`, `askSize` and `localTimestamp` — the names used
by the Tardis JSON API, not by the CSV datasets. None of them occurs in a real
export, so all four column indices stay at their `-1` initial value and the reader
then evaluates `cols[static_cast<std::size_t>(-1)]`, an out-of-bounds vector
access on the first data row of every real quotes file. The trades reader is
unaffected: `timestamp`, `price` and `amount` match the real names.

**Quote columns are mirrored outside-in** — `ask_amount,ask_price,bid_price,bid_amount`.
Name-based mapping handles this; position-based mapping would silently transpose
bid and ask.

**`quotes` and `book_ticker` have identical schemas**, byte for byte. They differ
in provenance, not shape: `quotes` is the top of book that Tardis reconstructs
from the L2 stream and publishes for every exchange, `book_ticker` is an
exchange's own best-bid/ask push channel where one exists. Both map to `Quote`.

## Configuration syntax

A data type is named by the key; the value is the file, resolved relative to the
directory of the INI file:

```ini
[data-source]
tardis.trades=bitmex_trades_2020-04-01_XBTUSD.csv.gz
tardis.quotes=bitmex_quotes_2020-04-01_XBTUSD.csv.gz
tardis.trades=bitmex_trades_2020-04-02_XBTUSD.csv.gz
tardis.quotes=bitmex_quotes_2020-04-02_XBTUSD.csv.gz
```

| Key | Data type | Event produced |
|---|---|---|
| `tardis.trades` | `trades` | `Trade` |
| `tardis.quotes` | `quotes`, `book_ticker` | `Quote` |

Both keys may repeat, in any order, and apply only within the configuration file
that names them. `tardis.quotes` accepts a `book_ticker` export as well, because
the schema is the same; the key is named for the event it produces rather than
for one vendor data type.

**There are no `tardis.*` option keys.** Every setting that a source could need
resolved to a fixed answer:

| Candidate option | Why it does not exist |
|---|---|
| `tardis.symbol` | the symbol comes from the file's own columns; renaming is what `[symbol-mapping]` is for |
| `tardis.time_unit` | Tardis publishes microseconds, there is nothing to choose |
| `tardis.time_column` | always `local_timestamp`, see below |
| `tardis.exchange` | an export is one exchange by construction, so a filter would match everything or nothing |

Tardis therefore uses the `<source>.<name>=` scheme for data keys only and carries
no option block. The per-file scoping and order independence that the scheme
guarantees are still satisfied — trivially, because there is no state to carry.

The `tardis=` key is retained as a branch that throws, naming the two keys that
replace it. Removing it outright would report `Unknown key tardis`, which does not
say what to do instead.

### Event time

The event time is `local_timestamp`, the instant the Tardis collector received the
message, not `timestamp`, the exchange's own clock. Two reasons:

- A strategy cannot act on a message before it arrives, so `local_timestamp` is
  the instant at which the information was available.
- `MergedDataSource` is a heap over inputs it assumes are sorted, and an unsorted
  input silently corrupts the merged timeline. `local_timestamp` is monotonic by
  construction; exchange clocks jitter.

The `timestamp` column is consequently unused and is not required to be present.

## Architecture

No new component directory. `src/quarkbot/tardis/` already builds into
`quarkbot_impl` unconditionally — `src/quarkbot/CMakeLists.txt:19` adds it with no
option guard and `QUARKBOT_TARDIS` never reaches the generated
`quarkbot_compile_config.h` — so the wiring needs no conditional compilation, as
the algoseek wiring already demonstrates. (`CLAUDE.md` still describes the
component as gated behind `QUARKBOT_TARDIS`; that description is stale and is
corrected as part of this work.)

| Unit | Responsibility |
|---|---|
| `TardisCsvDataSource` | gzip line reading, header mapping, `exchange:symbol` |
| `TardisTradesDataSource` | trades row → `Trade` |
| `TardisQuotesDataSource` | quotes / book_ticker row → `Quote` |
| `SourceCollector::walk` | `tardis.*` keys → constructed sources |

### Changes to the source classes

**A move constructor for `TardisCsvDataSource`**, taking `_gz` and leaving the
source `nullptr`. This is a hard prerequisite, not a cleanup: the user-declared
destructor suppresses the implicit move constructor and the copy constructor is
deleted, so the class is currently neither copyable nor movable, and
`BacktestDataSource` is a `std::move_only_function` which requires move
construction. Verified with
`static_assert(!std::is_move_constructible_v<TardisTradesDataSource>)`, which
holds today.

**The symbol comes from the file.** `exchange` and `symbol` of the first data row
are joined as `exchange + ":" + symbol` — `bitmex:XBTUSD`,
`binance-futures:ETHUSDT` — and cached. Qualifying by exchange keeps the same pair
from two venues apart, and `[symbol-mapping]` renames it to whatever a strategy
expects. The `instrument` constructor parameter becomes dead and is removed.

**Header mapping moves into the constructor** and a missing required column throws
there, listing what is missing. Today the header is mapped lazily and a missing
column is the out-of-bounds access described above.

Required columns are `exchange`, `symbol`, `local_timestamp`, plus `price` and
`amount` for trades, or `bid_price`, `bid_amount`, `ask_price` and `ask_amount`
for quotes. `timestamp` is not required.

The header is therefore validated at construction while the symbol is only known
once the first data row is read. A file holding nothing but a valid header is a
legitimately empty source: it constructs, the first call returns `false`, and no
symbol is ever produced.

**`side` is mapped.** Trades carry `buy`/`sell`, `Trade::side` is documented as the
taker's side, and `string_lookup<Side>` (`include/quarkbot/types.hpp:38`) already
maps exactly those two strings. The lookup table is bidirectional
(`utils/lookup.hpp:27`), so `string_lookup<Side>("buy")` yields
`std::optional<Side>` directly. A value that is neither string is a row error; an
absent `side` column is not, since only trades have one and `Side::undetermined`
is a valid trade side. The current reader discards the column.

**Bad rows throw instead of being skipped.** `catch (...) { continue; }` currently
drops any row whose numbers fail to parse, without a counter or a message. It is
replaced by an error naming the file, the row number and the column.

**Ordering and identity are checked per row**: a `local_timestamp` below the
previous one, or an `exchange:symbol` differing from the first row, throws. The
first guards the merged timeline against a reordered file; the second catches two
instruments concatenated into one file, which would otherwise be folded into a
single instrument. Both mirror checks the Algoseek source already performs.

### Wiring

```cpp
else if (row.key.starts_with("tardis.")) {
    auto t = row.key.substr(7);
    if (t == "trades")      sources.push_back(TardisTradesDataSource(root/row.value));
    else if (t == "quotes") sources.push_back(TardisQuotesDataSource(root/row.value));
    else throw std::runtime_error(std::format(
        "Unknown tardis data type: `{}`, Expected: trades, quotes in config `{}`",
        row.key, fcan.string()));
}
else if (row.key == "tardis") throw std::runtime_error(std::format(
    "Key `tardis` is no longer supported, use `tardis.trades` or `tardis.quotes` "
    "in config `{}`", fcan.string()));
```

Sources are constructed on sight. The two-phase collect that algoseek needs — read
the whole file, then apply the options — has no purpose here, because there are no
options to apply.

### Data flow

```
[data-source]
tardis.trades=…_trades_….csv.gz
tardis.quotes=…_quotes_….csv.gz
    │
    ▼
SourceCollector::walk  →  one source per key, symbol read from each file
    │
    ▼
MergedDataSource  →  SymbologyMapping  →  BacktestDataSource
```

Nothing pairs the trades file with the quotes file. Both report
`exchange:symbol` from their own columns, both land in the same
`MergedDataSource`, and the simulation resolves one instrument from the shared
symbol. This is the property the whole design rests on, and the only place it can
be observed is a test that composes the two sources.

## Error handling

| Stage | Condition | Reaction |
|---|---|---|
| configuration | unknown `tardis.*` data type | `runtime_error` with the key and the config file |
| configuration | legacy `tardis=` key | `runtime_error` naming the replacement keys |
| construction | `gzopen` fails | `runtime_error` with the path |
| construction | a required column is missing | `runtime_error` listing the missing columns |
| row | `price`/`amount`/quote number does not parse | `runtime_error` with file, row and column |
| row | `local_timestamp` below the previous row | `runtime_error` |
| row | `exchange:symbol` differs from the first row | `runtime_error` naming both symbols |

Nothing is skipped silently. Unlike the Algoseek source, there are no
legitimately-discarded rows in these formats, so there are no skip counters and no
EOF summary.

## Testing

**`src/tests/tardis_source_test.cpp`** — rewritten. Its current synthetic data
encodes the camelCase JSON schema and nanosecond timestamps, so it passes while
testing a format Tardis has never published; the reader and the test were derived
from the same wrong reference and confirmed each other. New synthetic cases use
the real header, with `local_timestamp` deliberately different from `timestamp` so
that reading the wrong column fails the test, and cover: `Trade` with `side`
mapped from `buy`/`sell`; `Quote` with bid and ask not transposed; symbol
`bitmex:XBTUSD`; a header-only file as an empty source; and one negative case for
each construction-stage and row-stage entry of the error table. The two
configuration-stage entries belong to the configuration test below.

**Truncated real exports as fixtures.** The full files in `data/` are 85 MB, 22 MB
and 6.9 MB and do not belong in git. The first 2000 data rows of each, re-gzipped
into `src/tests/data/`, come to roughly 100 KB in total — the same order as the
Algoseek fixtures already there (60 KB and 8 KB) — and the test asserts concrete
values against them: first and last event, event count, symbol, and `side` for
trades. This fixture is the only part of the test suite not derived from someone's
reading of the vendor's documentation, which is precisely what made the existing
green test worthless.

`data/` is added to `.gitignore` so the full exports cannot enter the history by
accident.

**`src/tests/config_datasource_test.cpp`** — a second test function beside the
symbol-mapping one added in `bfb4624`. One INI naming a `tardis.trades` and a
`tardis.quotes` file for the same instrument must yield one interleaved timeline
under one symbol; the two fixtures must overlap in time, because a test whose
quotes all precede its trades would pass with a broken heap. Plus an unknown
`tardis.*` data type and the legacy `tardis=` key as error cases, each asserting
the config file is named.

## Deferred work

**`split_csv` allocates a vector per row.** The book_ticker export is 9.4 million
rows for one day of one instrument, so this is one heap allocation per row at that
scale. It is a measurable cost rather than a correctness problem, and replacing it
with `CSVReader`/`CSVFieldIndexMapping` as the Algoseek source does would be a
larger change than everything else in this design combined. Left for when it is
measured to matter.

**`book_snapshot_*` and `incremental_book_L2`** become `tardis.book_snapshot` and
`tardis.incremental_book` keys. Increments additionally need order book state and
a rule for the initial snapshot.
