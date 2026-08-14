#pragma once

#include "quarkbot/abstract/backtest_data_source.hpp"
#include <filesystem>
namespace quarkbot {



///Reads all data sources from all configurations
/**
Top level section:  

include=file - can repeat for every include

Recognised keys of the data source section (the text after ';' explains the key,
it must not be written into a real configuration file, see the note below):

[data-source]
quarkbot=file.gz     ;quarkbot replay format  - gzip + CSV
tardis.trades=file.csv.gz  ;tardis.dev trades export - gzip CSV
tardis.quotes=file.csv.gz  ;tardis.dev quotes or book_ticker export - gzip CSV
lseg=file.gz         ;lseg trades, quotes, auctions gzip
trth=file.gz         ;trth trades, quotes, auctions gzip
algoseek=file.csv.gz ;algoseek US equity "Trades Only" export - gzip CSV

The algoseek key reads an Algoseek US equity "Trades Only" export (one file per
ticker per day) and produces Trade and final Auction events. It is configured by
separate option keys rather than by parameters on the value, so that one option
block can serve many files:

[data-source]
algoseek.time_zone=America/New_York
algoseek.exchange=NASDAQ
algoseek=20230601/IBM.csv.gz
algoseek=20230602/IBM.csv.gz
algoseek=20230605/IBM.csv.gz

  algoseek.time_zone - IANA name of the zone the files' wall clock timestamps
                       are in. Defaults to UTC; real exports are in
                       America/New_York, so omitting it shifts every timestamp
                       by four or five hours.
  algoseek.exchange  - emit only rows of this venue, matched verbatim and case
                       sensitively against the Exchange column. Without it
                       every venue is replayed, including the off-exchange
                       prints reported under FINRA.
  algoseek.symbol    - symbol reported on events, instead of the Ticker column.
                       Needed when the same ticker is replayed from two venues,
                       which would otherwise collide on one instrument.

All three options are optional and may appear anywhere in the section, before or
after the algoseek keys they configure - they are collected first and applied to
every algoseek key of the same configuration file afterwards. Repeating an
option overwrites the previous value; an unknown algoseek.* key is an error.

The option block is per configuration file and is not inherited by or from
included files. To replay the same ticker from two venues, or two tickers with
different settings, put each group in its own file and pull them in with
include= - each included file then carries its own option block.

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

Each tardis.trades/tardis.quotes file must already be sorted by local_timestamp
and cover a single instrument: a row whose local_timestamp is lower than the
previous row's, or whose exchange/symbol differs from the file's first row, is
rejected with an error rather than accepted silently. Concatenating two daily
exports, or two instruments, into one file will hit this - keep them as
separate files (and separate tardis.trades=/tardis.quotes= lines) instead.

Do not write a trailing comment after a value: only a line starting with ';'
or '#' is treated as a comment, so it would become part of the value.

[symbol-mapping]
SYM1=>SYM2
SYM3<=SYM4

keys can be duplicated

Relative paths are resolved relative to configuration file

*/

enum class SymbologyMapMode {
    ///ignore symbol mapping configuration
    no_mapping,
    ///ignore missing mapping, not-mapped symbols appear under original names
    ignore_missing,
    ///skip events with symbols not mapped to symbology.
    skip_missing,
};

///Read configuration files and create BacktestDataSource containing all sources merged to single timeline
/**
@param ini_config path to config
@param smm symbologyMappingMode
@param data_section name of data-source section
@param symbology_mapping_section name of symbol-mapping section.

Function process all files referenced by include= key (must be specified above all sections)
*/
BacktestDataSource configure_datasources(std::filesystem::path ini_config, 
        SymbologyMapMode smm = SymbologyMapMode::ignore_missing,
        std::string_view data_section = "data-source",
        std::string_view symbology_mapping_section = "symbol-mapping"
        
);


}