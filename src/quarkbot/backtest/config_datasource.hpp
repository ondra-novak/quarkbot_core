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