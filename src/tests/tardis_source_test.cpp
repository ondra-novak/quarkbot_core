#include "libs/tardis/tardis_data_source.hpp"
#include "tests/check.h"
#include <zlib.h>
#include <cstdio>
#include <fstream>
#include <iostream>

static void write_gz(const std::string &path, std::string_view content) {
    gzFile f = gzopen(path.c_str(), "wb");
    if (!f) { std::cerr << "Cannot open gz for write: " << path << std::endl; exit(1); }
    gzwrite(f, content.data(), static_cast<unsigned>(content.size()));
    gzclose(f);
}

static void write_file(const std::string &path, std::string_view content) {
    std::ofstream f(path);
    f << content;
}

static const std::string_view INI_SPOT = R"(
[instrument]
name=BTCUSDT
type=spot
quote_currency=USD
pnl_currency=USD
asset_wallet=BTC
min_lot_size=0.00001
lot_size_increment=0.00001
price_increment=0.01
fee_rate_maker=0.001
fee_rate_taker=0.001
)";

static const std::string_view TRADES_CSV =
    "exchange,symbol,timestamp,localTimestamp,id,side,price,amount\n"
    "binance,BTCUSDT,1617235200000000000,1617235200001000000,1,buy,58000.50,0.001\n"
    "binance,BTCUSDT,1617235260000000000,1617235260001000000,2,sell,58100.00,0.002\n";

static const std::string_view QUOTES_CSV =
    "exchange,symbol,timestamp,localTimestamp,bidPrice,bidSize,askPrice,askSize\n"
    "binance,BTCUSDT,1617235200000000000,1617235200001000000,57999.00,1.5,58001.00,0.8\n"
    "binance,BTCUSDT,1617235260000000000,1617235260001000000,58050.00,2.0,58052.00,1.2\n";

int main() {
    // --- Trades test ---
    write_file("/tmp/test_trades.ini",
        std::string(INI_SPOT) + "\n[source]\nfile=/tmp/test_trades.csv.gz\n");
    write_gz("/tmp/test_trades.csv.gz", TRADES_CSV);

    {
        quarkbot::TardisTradesDataSource src("/tmp/test_trades.ini");

        // Test instrument spec from INI
        auto specs = src.get_instrument_infos();
        CHECK_EQUAL(specs.size(), std::size_t(1));
        CHECK_EQUAL(specs[0].name, std::string("BTCUSDT"));
        CHECK(specs[0].type == quarkbot::InstrumentType::spot);
        CHECK_EQUAL(specs[0].quote_currency, std::string("USD"));
        CHECK_EQUAL(specs[0].pnl_currency, std::string("USD"));
        CHECK(specs[0].asset_wallet.has_value());
        CHECK_EQUAL(*specs[0].asset_wallet, std::string("BTC"));
        CHECK(specs[0].fee_rate_maker == Decimal::from_string("0.001"));
        CHECK(specs[0].fee_rate_taker == Decimal::from_string("0.001"));

        // Test reading trade events
        auto e1 = src.next_event();
        CHECK(e1.has_value());
        CHECK_EQUAL(e1->instrument, std::string("BTCUSDT"));
        CHECK(std::holds_alternative<quarkbot::Trade>(e1->payload));
        auto &t1 = std::get<quarkbot::Trade>(e1->payload);
        CHECK(t1.price == Decimal::from_string("58000.50"));
        CHECK(t1.size == Decimal::from_string("0.001"));

        auto e2 = src.next_event();
        CHECK(e2.has_value());
        auto &t2 = std::get<quarkbot::Trade>(e2->payload);
        CHECK(t2.price == Decimal::from_string("58100.00"));
        CHECK(t2.size == Decimal::from_string("0.002"));

        auto e3 = src.next_event();
        CHECK(!e3.has_value());
    }

    std::remove("/tmp/test_trades.ini");
    std::remove("/tmp/test_trades.csv.gz");

    // --- Quotes test ---
    write_file("/tmp/test_quotes.ini",
        std::string(INI_SPOT) + "\n[source]\nfile=/tmp/test_quotes.csv.gz\n");
    write_gz("/tmp/test_quotes.csv.gz", QUOTES_CSV);

    {
        quarkbot::TardisQuotesDataSource qsrc("/tmp/test_quotes.ini");
        auto specs = qsrc.get_instrument_infos();
        CHECK_EQUAL(specs.size(), std::size_t(1));

        auto q1 = qsrc.next_event();
        CHECK(q1.has_value());
        CHECK_EQUAL(q1->instrument, std::string("BTCUSDT"));
        CHECK(std::holds_alternative<quarkbot::Quote>(q1->payload));
        auto &quote1 = std::get<quarkbot::Quote>(q1->payload);
        CHECK(quote1.bid == Decimal::from_string("57999.00"));
        CHECK(quote1.bid_size == Decimal::from_string("1.5"));
        CHECK(quote1.ask == Decimal::from_string("58001.00"));
        CHECK(quote1.ask_size == Decimal::from_string("0.8"));

        auto q2 = qsrc.next_event();
        CHECK(q2.has_value());
        auto &quote2 = std::get<quarkbot::Quote>(q2->payload);
        CHECK(quote2.bid == Decimal::from_string("58050.00"));
        CHECK(quote2.bid_size == Decimal::from_string("2.0"));
        CHECK(quote2.ask == Decimal::from_string("58052.00"));
        CHECK(quote2.ask_size == Decimal::from_string("1.2"));

        auto q3 = qsrc.next_event();
        CHECK(!q3.has_value());
    }

    std::remove("/tmp/test_quotes.ini");
    std::remove("/tmp/test_quotes.csv.gz");

    std::cout << "All tardis source tests passed" << std::endl;
}
