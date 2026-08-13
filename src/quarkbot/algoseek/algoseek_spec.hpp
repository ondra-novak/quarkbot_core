#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace quarkbot {

///Parsed specification of an Algoseek data source
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

///Parse a data source specification
/**
    Syntax: <file>[?exchange=<name>&tzone=<iana zone>&symbol=<symbol>]

    All parameters are optional. A missing exchange means no filtering, a missing
    tzone means UTC, a missing symbol means the Ticker column is used.

    @param spec specification string, as written in the INI configuration
    @return parsed specification with a relative file path
    @exception std::runtime_error empty path, malformed parameter, unknown
        parameter key, or unknown time zone name
*/
//AlgoseekSpec parse_algoseek_spec(std::string_view spec);

}
