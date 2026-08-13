#pragma once

#include "quarkbot/abstract/backtest_data_source.hpp"
#include <filesystem>
namespace quarkbot {



///Reads all data sources from all configurations
/**
Top level section:  

include=file - can repeat for every include

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