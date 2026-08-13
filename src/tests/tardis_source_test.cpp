#include "quarkbot/tardis/tardis_data_source.hpp"
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "tests/check.h"
#include <zlib.h>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>

static void write_gz(const std::string &path, std::string_view content) {
    gzFile f = gzopen(path.c_str(), "wb");
    if (!f) { std::cerr << "Cannot open gz for write: " << path << std::endl; exit(1); }
    gzwrite(f, content.data(), static_cast<unsigned>(content.size()));
    gzclose(f);
}

///parse "2020-04-01 00:00:03.089980" into a time_point
static std::chrono::system_clock::time_point mk_utc(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::chrono::sys_time<std::chrono::microseconds> tp;
    in >> std::chrono::parse("%F %T", tp);
    if (in.fail()) { std::cerr << "bad test instant: " << text << std::endl; exit(1); }
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(tp);
}

// the real CSV header, with local_timestamp deliberately 1 second after
// timestamp so that reading the wrong column fails the assertions
static const std::string_view TRADES_CSV =
    "exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n"
    "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,buy,6425.5,12\n"
    "bitmex,XBTUSD,1585699203957000,1585699204957000,bbb,sell,6425,150\n";

static const std::string_view QUOTES_CSV =
    "exchange,symbol,timestamp,local_timestamp,ask_amount,ask_price,bid_price,bid_amount\n"
    "huobi-dm-swap,BTC-USD,1585699201147000,1585699202147000,86,6423,6422.9,112\n"
    "huobi-dm-swap,BTC-USD,1585699202147000,1585699203147000,88,6424,6423.9,114\n";

static void test_trades() {
    using namespace quarkbot;
    write_gz("/tmp/test_tardis_trades.csv.gz", TRADES_CSV);
    TardisTradesDataSource src("/tmp/test_tardis_trades.csv.gz");

    BacktestEvent e1;
    CHECK(src(e1));
    CHECK(std::holds_alternative<Trade>(e1.data));
    // microseconds, and local_timestamp rather than timestamp
    CHECK(e1.time == mk_utc("2020-04-01 00:00:03.957000"));
    CHECK(std::get<Trade>(e1.data).price == Decimal::from_string("6425.5"));
    CHECK(std::get<Trade>(e1.data).size == Decimal::from_string("12"));
    CHECK_EQUAL(e1.symbol, std::string("bitmex:XBTUSD"));
    CHECK(std::get<Trade>(e1.data).side == Side::buy);

    BacktestEvent e2;
    CHECK(src(e2));
    CHECK(e2.time == mk_utc("2020-04-01 00:00:04.957000"));
    CHECK(std::get<Trade>(e2.data).side == Side::sell);
    BacktestEvent e3;
    CHECK(!src(e3));
}

static void test_quotes() {
    using namespace quarkbot;
    write_gz("/tmp/test_tardis_quotes.csv.gz", QUOTES_CSV);
    TardisQuotesDataSource src("/tmp/test_tardis_quotes.csv.gz");

    BacktestEvent q1;
    CHECK(src(q1));
    CHECK(std::holds_alternative<Quote>(q1.data));
    CHECK(q1.time == mk_utc("2020-04-01 00:00:02.147000"));
    auto &quote = std::get<Quote>(q1.data);
    // the columns are mirrored outside-in; bid and ask must not be transposed
    CHECK(quote.bid == Decimal::from_string("6422.9"));
    CHECK(quote.bid_size == Decimal::from_string("112"));
    CHECK(quote.ask == Decimal::from_string("6423"));
    CHECK(quote.ask_size == Decimal::from_string("86"));
    CHECK_EQUAL(q1.symbol, std::string("huobi-dm-swap:BTC-USD"));
}

static void test_construction_errors() {
    using namespace quarkbot;
    // amount is absent: today this indexes cols[size_t(-1)], it must throw instead
    write_gz("/tmp/test_tardis_nocol.csv.gz",
        "exchange,symbol,timestamp,local_timestamp,id,side,price\n"
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,buy,6425.5\n");
    CHECK_EXCEPTION_EXPR(std::runtime_error, e,
        std::string_view(e.what()).find("amount") != std::string_view::npos,
        TardisTradesDataSource src("/tmp/test_tardis_nocol.csv.gz"));

    // a nonexistent file fails when the source is constructed, naming the path
    CHECK_EXCEPTION_EXPR(std::runtime_error, e,
        std::string_view(e.what()).find("does_not_exist_tardis") != std::string_view::npos,
        TardisTradesDataSource src("/tmp/does_not_exist_tardis.csv.gz"));
}

///a valid header with no data rows is an empty source, not an error
static void test_header_only() {
    using namespace quarkbot;
    write_gz("/tmp/test_tardis_headeronly.csv.gz",
        "exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n");
    TardisTradesDataSource src("/tmp/test_tardis_headeronly.csv.gz");
    BacktestEvent ev;
    CHECK(!src(ev));
}

static void test_real_exports() {
    using namespace quarkbot;
    const std::filesystem::path dir = TEST_DATA_PATH;

    // bitmex trades, 2000 rows
    {
        TardisTradesDataSource src(
            dir/"bitmex_trades_2020-04-01_XBTUSD.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK(ev.time == mk_utc("2020-04-01 00:00:03.089980"));
        CHECK(std::get<Trade>(ev.data).price == Decimal::from_string("6425.5"));
        CHECK_EQUAL(ev.symbol, std::string("bitmex:XBTUSD"));
        CHECK(std::get<Trade>(ev.data).side == Side::buy);   // both boundary rows are buys
        std::size_t n = 1;
        while (src(ev)) ++n;
        CHECK_EQUAL(n, std::size_t(2000));
        CHECK(ev.time == mk_utc("2020-04-01 00:00:21.951810"));
        CHECK(std::get<Trade>(ev.data).price == Decimal::from_string("6428.5"));
        CHECK(std::get<Trade>(ev.data).size == Decimal::from_string("2000"));
        CHECK_EQUAL(ev.symbol, std::string("bitmex:XBTUSD"));
        CHECK(std::get<Trade>(ev.data).side == Side::buy);   // both boundary rows are buys
    }
    // huobi quotes, 2000 rows
    {
        TardisQuotesDataSource src(
            dir/"huobi-dm-swap_quotes_2020-04-01_BTC-USD.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK(ev.time == mk_utc("2020-04-01 00:00:01.270777"));
        CHECK(std::get<Quote>(ev.data).bid == Decimal::from_string("6422.9"));
        CHECK(std::get<Quote>(ev.data).ask == Decimal::from_string("6423"));
        CHECK_EQUAL(ev.symbol, std::string("huobi-dm-swap:BTC-USD"));
        std::size_t n = 1;
        while (src(ev)) ++n;
        CHECK_EQUAL(n, std::size_t(2000));
        CHECK(ev.time == mk_utc("2020-04-01 00:03:53.783935"));
        CHECK(std::get<Quote>(ev.data).bid == Decimal::from_string("6432.4"));
        CHECK(std::get<Quote>(ev.data).ask == Decimal::from_string("6432.5"));
        CHECK_EQUAL(ev.symbol, std::string("huobi-dm-swap:BTC-USD"));
    }
    // binance-futures book_ticker: same schema as quotes, read by the same class
    {
        TardisQuotesDataSource src(
            dir/"binance-futures_book_ticker_2024-04-01_ETHUSDT.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK(ev.time == mk_utc("2024-04-01 00:00:00.011559"));
        CHECK(std::get<Quote>(ev.data).bid == Decimal::from_string("3648.79"));
        CHECK(std::get<Quote>(ev.data).ask == Decimal::from_string("3648.8"));
        CHECK_EQUAL(ev.symbol, std::string("binance-futures:ETHUSDT"));
        std::size_t n = 1;
        while (src(ev)) ++n;
        CHECK_EQUAL(n, std::size_t(2000));
        CHECK(ev.time == mk_utc("2024-04-01 00:00:14.408782"));
        CHECK(std::get<Quote>(ev.data).bid == Decimal::from_string("3646.29"));
        CHECK_EQUAL(ev.symbol, std::string("binance-futures:ETHUSDT"));
    }
}

///a trades file with no side column is valid; side stays undetermined
static void test_side_optional() {
    using namespace quarkbot;
    write_gz("/tmp/test_tardis_noside.csv.gz",
        "exchange,symbol,timestamp,local_timestamp,price,amount\n"
        "bitmex,XBTUSD,1585699202957000,1585699203957000,6425.5,12\n");
    TardisTradesDataSource src("/tmp/test_tardis_noside.csv.gz");
    BacktestEvent ev;
    CHECK(src(ev));
    CHECK(std::get<Trade>(ev.data).side == Side::undetermined);
}

static void test_row_errors() {
    using namespace quarkbot;
    const std::string_view head =
        "exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n";

    // a non-numeric price names the file, the row and the column
    // (the temp file is named so that "price" cannot appear via the path,
    // and the message check requires "column price" together with the raw
    // bad value, neither of which the path could supply)
    write_gz("/tmp/test_tardis_badnum.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,buy,abc,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_badnum.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("column price") != std::string_view::npos
            && std::string_view(e.what()).find("'abc'") != std::string_view::npos
            && std::string_view(e.what()).find("test_tardis_badnum.csv.gz") != std::string_view::npos,
            src(ev));
    }
    // a short row
    write_gz("/tmp/test_tardis_short.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699203957000\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_short.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }
    // an unrecognised side
    write_gz("/tmp/test_tardis_badside.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,sideways,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_badside.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("sideways") != std::string_view::npos,
            src(ev));
    }
    // a non-numeric timestamp must not silently become epoch 0
    write_gz("/tmp/test_tardis_badtime.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,notanumber,aaa,buy,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_badtime.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("local_timestamp") != std::string_view::npos,
            src(ev));
    }
    // an empty timestamp, same reasoning
    write_gz("/tmp/test_tardis_emptytime.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,,aaa,buy,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_emptytime.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }
    // a timestamp too large for system_clock::duration: accumulating it would
    // overflow, and duration_cast to nanoseconds multiplies by another 1000
    write_gz("/tmp/test_tardis_hugetime.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,99999999999999999999,aaa,buy,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_hugetime.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }
    // a negative timestamp: std::from_chars accepts a leading '-'
    write_gz("/tmp/test_tardis_negtime.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,-1585699203957000,aaa,buy,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_negtime.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }
    // the row number identifies the offending line; the header is row 1
    write_gz("/tmp/test_tardis_row3.csv.gz", std::string(head) +
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,buy,6425.5,12\n"
        "bitmex,XBTUSD,1585699203957000,1585699204957000,bbb,buy,xyz,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_row3.csv.gz");
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("row 3") != std::string_view::npos,
            src(ev));
    }
    // side is present but placed LAST in the header, so _col_side is the
    // highest mapped index; a row that stops one column short of it (present
    // in every other required column) must still be a truncated row, not a
    // silent Side::undetermined
    write_gz("/tmp/test_tardis_sideshort.csv.gz",
        "exchange,symbol,timestamp,local_timestamp,id,price,amount,side\n"
        "bitmex,XBTUSD,1585699202957000,1585699203957000,aaa,6425.5,12\n");
    {
        TardisTradesDataSource src("/tmp/test_tardis_sideshort.csv.gz");
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }
}

static void test_movable() {
    using namespace quarkbot;
    static_assert(std::is_move_constructible_v<TardisTradesDataSource>);
    static_assert(std::is_move_constructible_v<TardisQuotesDataSource>);
    static_assert(BacktestDataSourceType<TardisTradesDataSource>);

    write_gz("/tmp/test_tardis_movable.csv.gz", TRADES_CSV);
    // the point of the move constructor: a source has to survive being put here
    BacktestDataSource ds = TardisTradesDataSource("/tmp/test_tardis_movable.csv.gz");
    BacktestEvent ev;
    CHECK(ds(ev));
    CHECK_EQUAL(ev.symbol, std::string("bitmex:XBTUSD"));
    std::remove("/tmp/test_tardis_movable.csv.gz");
}

int main() {
    test_trades();
    test_quotes();
    test_movable();
    test_construction_errors();
    test_header_only();
    test_real_exports();
    test_side_optional();
    test_row_errors();

    std::cout << "All tardis source tests passed" << std::endl;
}
