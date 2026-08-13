#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace quarkbot {

///Specification of an Algoseek data source
/**
    Filled by SourceCollector::walk() from the `algoseek` and `algoseek.*` keys
    of the data source section, see config_datasource.hpp. Every field except
    the file path is optional; the defaults mean no exchange filtering, UTC
    timestamps and the Ticker column as the symbol.
*/
struct AlgoseekSpec {
    ///path to the gzipped CSV file; relative paths must be resolved by the caller
    std::filesystem::path file;
    ///exchange filter matched verbatim against the Exchange column, empty means no filtering
    std::string exchange = {};
    ///time zone of the file's wall clock timestamps
    const std::chrono::time_zone *tz = nullptr;
    ///symbol reported on emitted events, empty means use the Ticker column
    std::string symbol = {};
};

}
