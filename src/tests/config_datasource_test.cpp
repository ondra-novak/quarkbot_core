#include "quarkbot/backtest/config_datasource.hpp"

#include "check.h"
#include "quarkbot/abstract/backtest_data_source.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <zlib.h>

using namespace quarkbot;

namespace {

void write_gz(const std::string &path, std::string_view content) {
    gzFile f = gzopen(path.c_str(), "wb");
    if (!f) { std::cerr << "Cannot open gz for write: " << path << std::endl; exit(1); }
    gzwrite(f, content.data(), static_cast<unsigned>(content.size()));
    gzclose(f);
}

///write a one-row Algoseek export reporting the given ticker
void write_source(const std::filesystem::path &path, std::string_view ticker) {
    write_gz(path.string(),
        std::string("Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange,Conditions\n")
        + "20230609,10:00:00.000000000,TRADE," + std::string(ticker)
        + ",47.60,100,NASDAQ,00000001\n");
}

///symbols of every event the configured source produces, joined by ','
std::string replay(const std::filesystem::path &config) {
    auto ds = configure_datasources(config);
    std::string symbols;
    BacktestEvent ev;
    while (ds(ev)) {
        if (!symbols.empty()) symbols.push_back(',');
        symbols.append(ev.symbol);
    }
    return symbols;
}

}

///`TARGET<=SOURCE` renames SOURCE to TARGET, same as `SOURCE=>TARGET`
static void test_symbol_mapping_directions() {
    std::cout << "--- test_symbol_mapping_directions" << std::endl;

    auto dir = std::filesystem::temp_directory_path() / "qb_config_datasource_test";
    std::filesystem::create_directories(dir);
    write_source(dir / "IBM.csv.gz", "IBM");

    {
        std::ofstream ini(dir / "arrow_right.ini");
        ini << "[data-source]\n"
               "algoseek=IBM.csv.gz\n"
               "[symbol-mapping]\n"
               "IBM=>IBM.NASDAQ\n";
    }
    CHECK_EQUAL(replay(dir / "arrow_right.ini"), std::string("IBM.NASDAQ"));

    {
        std::ofstream ini(dir / "arrow_left.ini");
        ini << "[data-source]\n"
               "algoseek=IBM.csv.gz\n"
               "[symbol-mapping]\n"
               "IBM.NASDAQ<=IBM\n";
    }
    CHECK_EQUAL(replay(dir / "arrow_left.ini"), std::string("IBM.NASDAQ"));

    // spaces around the arrow are part of neither symbol
    {
        std::ofstream ini(dir / "spaced.ini");
        ini << "[data-source]\n"
               "algoseek=IBM.csv.gz\n"
               "[symbol-mapping]\n"
               "IBM.NASDAQ <= IBM\n";
    }
    CHECK_EQUAL(replay(dir / "spaced.ini"), std::string("IBM.NASDAQ"));

    std::filesystem::remove_all(dir);
}

int main() {
    test_symbol_mapping_directions();
    std::cout << "All config datasource tests passed" << std::endl;
    return 0;
}
