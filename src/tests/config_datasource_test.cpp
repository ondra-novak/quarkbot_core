#include "quarkbot/backtest/config_datasource.hpp"

#include "check.h"
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <variant>
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
    auto [ds,_] = configure_datasources(config);
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

///one INI, two data types, one instrument
static void test_tardis_keys() {
    std::cout << "--- test_tardis_keys" << std::endl;

    auto dir = std::filesystem::temp_directory_path() / "qb_tardis_cfg_test";
    std::filesystem::create_directories(dir);

    write_gz((dir/"t.csv.gz").string(),
        "exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n"
        "bitmex,XBTUSD,1585699200000000,1585699201000000,a,buy,100,1\n"
        "bitmex,XBTUSD,1585699202000000,1585699203000000,b,sell,102,1\n");
    write_gz((dir/"q.csv.gz").string(),
        "exchange,symbol,timestamp,local_timestamp,ask_amount,ask_price,bid_price,bid_amount\n"
        "bitmex,XBTUSD,1585699201000000,1585699202000000,5,101,100.5,5\n"
        "bitmex,XBTUSD,1585699203000000,1585699204000000,5,103,102.5,5\n");
    {
        std::ofstream ini(dir/"both.ini");
        ini << "[data-source]\n"
               "tardis.quotes=q.csv.gz\n"
               "tardis.trades=t.csv.gz\n";
    }

    auto [ds,_] = configure_datasources(dir/"both.ini");
    std::string order;
    BacktestEvent ev;
    std::chrono::system_clock::time_point prev = {};
    while (ds(ev)) {
        CHECK_EQUAL(ev.symbol, std::string("bitmex:XBTUSD"));
        CHECK(ev.time >= prev);
        prev = ev.time;
        order.push_back(std::holds_alternative<quarkbot::Trade>(ev.data) ? 'T' : 'Q');
    }
    // trades and quotes interleave, which only happens if both landed in one heap
    CHECK_EQUAL(order, std::string("TQTQ"));

    // an unknown data type names the key and the config file
    {
        std::ofstream ini(dir/"badtype.ini");
        ini << "[data-source]\n"
               "tardis.orderbook=q.csv.gz\n";
    }
    CHECK_EXCEPTION_EXPR(std::runtime_error, e,
        std::string_view(e.what()).find("tardis.orderbook") != std::string_view::npos
        && std::string_view(e.what()).find("badtype.ini") != std::string_view::npos,
        configure_datasources(dir/"badtype.ini"));

    // the retired bare key says what to use instead
    {
        std::ofstream ini(dir/"legacy.ini");
        ini << "[data-source]\n"
               "tardis=t.csv.gz\n";
    }
    CHECK_EXCEPTION_EXPR(std::runtime_error, e,
        std::string_view(e.what()).find("tardis.trades") != std::string_view::npos
        && std::string_view(e.what()).find("legacy.ini") != std::string_view::npos,
        configure_datasources(dir/"legacy.ini"));

    std::filesystem::remove_all(dir);
}

int main() {
    test_symbol_mapping_directions();
    test_tardis_keys();
    std::cout << "All config datasource tests passed" << std::endl;
    return 0;
}
