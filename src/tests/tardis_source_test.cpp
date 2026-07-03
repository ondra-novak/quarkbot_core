#include "quarkbot/tardis/tardis_data_source.hpp"
#include "tests/check.h"
#include <zlib.h>
#include <cstdio>
#include <iostream>

static void write_gz(const std::string &path, std::string_view content) {
    gzFile f = gzopen(path.c_str(), "wb");
    if (!f) { std::cerr << "Cannot open gz for write: " << path << std::endl; exit(1); }
    gzwrite(f, content.data(), static_cast<unsigned>(content.size()));
    gzclose(f);
}

static const std::string_view TRADES_CSV =
    "exchange,symbol,timestamp,localTimestamp,id,side,price,amount\n"
    "binance,BTCUSDT,1617235200000000000,1617235200001000000,1,buy,58000.50,0.001\n"
    "binance,BTCUSDT,1617235260000000000,1617235260001000000,2,sell,58100.00,0.002\n";

static const std::string_view QUOTES_CSV =
    "exchange,symbol,timestamp,localTimestamp,bidPrice,bidSize,askPrice,askSize\n"
    "binance,BTCUSDT,1617235200000000000,1617235200001000000,57999.00,1.5,58001.00,0.8\n"
    "binance,BTCUSDT,1617235260000000000,1617235260001000000,58050.00,2.0,58052.00,1.2\n";

int main() {
    using namespace quarkbot;

    // --- Trades test ---
    write_gz("/tmp/test_trades.csv.gz", TRADES_CSV);
    {
        TardisTradesDataSource src("BTCUSDT", "/tmp/test_trades.csv.gz");

        BacktestEvent e1;
        CHECK(src(e1));
        CHECK_EQUAL(e1.symbol, std::string("BTCUSDT"));
        CHECK(std::holds_alternative<Trade>(e1.data));
        auto &t1 = std::get<Trade>(e1.data);
        CHECK(t1.price == Decimal::from_string("58000.50"));
        CHECK(t1.size == Decimal::from_string("0.001"));

        BacktestEvent e2;
        CHECK(src(e2));
        auto &t2 = std::get<Trade>(e2.data);
        CHECK(t2.price == Decimal::from_string("58100.00"));
        CHECK(t2.size == Decimal::from_string("0.002"));

        BacktestEvent e3;
        CHECK(!src(e3));
    }
    std::remove("/tmp/test_trades.csv.gz");

    // --- Quotes test ---
    write_gz("/tmp/test_quotes.csv.gz", QUOTES_CSV);
    {
        TardisQuotesDataSource src("BTCUSDT", "/tmp/test_quotes.csv.gz");

        BacktestEvent q1;
        CHECK(src(q1));
        CHECK_EQUAL(q1.symbol, std::string("BTCUSDT"));
        CHECK(std::holds_alternative<Quote>(q1.data));
        auto &quote1 = std::get<Quote>(q1.data);
        CHECK(quote1.bid == Decimal::from_string("57999.00"));
        CHECK(quote1.bid_size == Decimal::from_string("1.5"));
        CHECK(quote1.ask == Decimal::from_string("58001.00"));
        CHECK(quote1.ask_size == Decimal::from_string("0.8"));

        BacktestEvent q2;
        CHECK(src(q2));
        auto &quote2 = std::get<Quote>(q2.data);
        CHECK(quote2.bid == Decimal::from_string("58050.00"));
        CHECK(quote2.bid_size == Decimal::from_string("2.0"));
        CHECK(quote2.ask == Decimal::from_string("58052.00"));
        CHECK(quote2.ask_size == Decimal::from_string("1.2"));

        BacktestEvent q3;
        CHECK(!src(q3));
    }
    std::remove("/tmp/test_quotes.csv.gz");

    std::cout << "All tardis source tests passed" << std::endl;
}
