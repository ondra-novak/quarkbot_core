# CSV history files

A history source serves historical data to a backtest from a set of CSV files, one
file per symbol. It answers `MarketInstrument::get_history().get_history<T>(request)`;
it is not a replay data source and produces no simulation events.

Each file may be plain `.csv` or gzipped `.csv.gz`; the extension decides which.
All files of one source hold the same type of data at the same interval.

## ini declaration

```
[history]
type=ohlc|close|auction|quote|l1
interval=m
index=index.csv
```

`index` is mandatory and is resolved relative to the configuration file. `type` is
mandatory. `interval` is optional and defaults to "no interval" (tick data), which
is what `close`, `quote`, `l1` and `auction` files hold.

One configuration file declares at most one `[history]` block; chain more of them
with `include=`, the same way the algoseek option block works.

### interval

The value is a count of seconds, a unit letter, or a count followed by a unit letter:

```
interval=s      1 second
interval=m      1 minute
interval=5m     5 minutes
interval=h      1 hour
interval=d      1 day
interval=w      1 week
interval=900    900 seconds
```

The value must match `HistoryDataRequest::interval` of the strategy's request
exactly, otherwise the source reports no data - a source declaring `interval=d`
never answers a request for minute bars.

## index

The index contains two columns:

```
symbol,file
```

Paths in the `file` column are relative to the index file. The symbols are the
symbols of the data vendor; `[symbol-mapping]` of the configuration renames them to
the symbols the strategy uses, exactly as it does for replay data sources. A symbol
that has no row in the index simply has no history.

## time column

Every data file has a `time` column, in the time zone of the instrument (the reader
performs no zone conversion). Accepted forms:

```
2026-06-26                    date only, midnight
2026-06-26 10:00:00           date and time separated by a space
2026-06-26T10:00:00           ISO 8601 separator
2026-06-26T10:00:00.123456    fractional seconds
2026-06-26T10:00:00Z          a trailing zone designator is accepted and ignored
```

Files must be sorted by time ascending. A record older than its predecessor is
reported as an error rather than silently truncating the stream.

The stream also ends at the simulation time, so that a backtest never reads its own
future. A record is cut by the moment it becomes known, which is not always its own
timestamp: an `ohlc` bar becomes known when it closes (`time` plus `interval`), so a
bar still forming at the simulation time is not reported. Every other type is a point
in time and becomes known when it happens.

## types

### ohlc

Serves `ClosedBar`.

```
time,open,high,low,close,volume
```

`time` is the opening time of the bar, `end_time` is derived from `interval`. Only
`time` and `close` are required; a missing `open`, `high` or `low` falls back to
`close` and a missing `volume` to zero. An optional `trades` column fills
`ClosedBar::trades`.

### close

Serves `Trade`.

```
time,close,volume
```

### auction

Serves `AuctionDailyHistory`.

```
time,open_price,open_volume,close_price,close_volume
```

Only the date part of `time` is used. A zero price means the auction did not firm.

### quote

Serves `Quote`.

```
time,ask_price,ask_volume,bid_price,bid_volume
```

### l1

Serves both `Quote` and `Trade` from one file.

```
time,ask_price,ask_volume,bid_price,bid_volume,price,volume
```

A request for `Quote` reads the bid/ask columns of every row. A request for `Trade`
reads `price`/`volume`, and rows whose `price` is zero carry no trade and are
skipped.
