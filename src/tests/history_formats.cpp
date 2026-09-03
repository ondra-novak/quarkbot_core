#include "../quarkbot/csv_history/formats.hpp"
#include "basic_coro/sync_await.hpp"
#include "quarkbot/csv_history/history_csv_source.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/hash/class_hash.hpp"
#include "quarkbot/stream/closedbar.hpp"
#include "quarkbot/timestamp.hpp"
#include "check.h"
#include <chrono>
#include <iostream>
#include <source_location>
#include <string>

template class quarkbot::HistoryOHLCReader<int (*)()>;
template class quarkbot::HistoryTradeReader<int (*)()>;
template class quarkbot::HistoryAuctionReader<int (*)()>;
template class quarkbot::HistoryQuoteReader<int (*)()>;
template class quarkbot::HistoryL1TradeReader<int (*)()>;

using namespace quarkbot;

namespace {

///a CSVReaderSource over a string, so the format readers can be tested without files
auto text_source(std::string text) {
    return [text = std::move(text), pos = std::size_t(0)]() mutable -> int {
        return pos >= text.size()?EOF:static_cast<unsigned char>(text[pos++]);
    };
}

Timestamp tp(std::string_view text) {
    return string2time(std::string(text));
}

}

///every documented form of the time column parses, anything else is an error
static void test_string2time() {
    std::cout << "--- test_string2time" << std::endl;

    CHECK_EQUAL(tp("2026-06-26"), tp("2026-06-26T00:00:00"));
    CHECK_EQUAL(tp("2026-06-26 10:00:00"), tp("2026-06-26T10:00:00"));
    CHECK_EQUAL(tp("2026-06-26T10:00:00Z"), tp("2026-06-26T10:00:00"));
    CHECK_EQUAL(tp("2026-06-26T00:00:01") - tp("2026-06-26"), std::chrono::seconds(1));
    CHECK_EQUAL(tp("2026-06-27") - tp("2026-06-26"), std::chrono::hours(24));

    //fractional seconds are kept, the timestamp resolution is below a second
    CHECK_GREATER(tp("2026-06-26T10:00:00.500"), tp("2026-06-26T10:00:00"));
    CHECK_LESS(tp("2026-06-26T10:00:00.500"), tp("2026-06-26T10:00:01"));

    CHECK_EXCEPTION(std::runtime_error, tp(""));
    CHECK_EXCEPTION(std::runtime_error, tp("not a date"));
    CHECK_EXCEPTION(std::runtime_error, tp("26/06/2026"));
    CHECK_EXCEPTION(std::runtime_error, tp("1750000000"));
}

///`interval` accepts a count of seconds, a unit letter, or a count with a unit letter
static void test_parse_interval() {
    std::cout << "--- test_parse_interval" << std::endl;

    CHECK_EQUAL(parse_history_interval(""), HistoryDataRequest::interval_undefined);
    CHECK_EQUAL(parse_history_interval("s"), HistoryDataRequest::interval_second);
    CHECK_EQUAL(parse_history_interval("m"), HistoryDataRequest::interval_minute);
    CHECK_EQUAL(parse_history_interval("h"), HistoryDataRequest::interval_hour);
    CHECK_EQUAL(parse_history_interval("d"), HistoryDataRequest::interval_day);
    CHECK_EQUAL(parse_history_interval("w"), HistoryDataRequest::interval_week);
    CHECK_EQUAL(parse_history_interval("5m"), 5*HistoryDataRequest::interval_minute);
    CHECK_EQUAL(parse_history_interval("1h"), HistoryDataRequest::interval_hour);
    CHECK_EQUAL(parse_history_interval("900"), 900U);
    CHECK_EQUAL(parse_history_interval(" d "), HistoryDataRequest::interval_day);

    CHECK_EXCEPTION(std::runtime_error, parse_history_interval("x"));
    CHECK_EXCEPTION(std::runtime_error, parse_history_interval("5x"));
    CHECK_EXCEPTION(std::runtime_error, parse_history_interval("min"));
}

///ohlc needs only time and close, the remaining columns fall back to close
static void test_ohlc_reader() {
    std::cout << "--- test_ohlc_reader" << std::endl;

    {
        HistoryOHLCReader reader(text_source(
            "time,open,high,low,close,volume,trades\n"
            "2026-06-26T09:00:00,25,26,23,24,77,4\n"
            "2026-06-26T09:01:00,24,24,20,21,55,2\n"), std::chrono::seconds(60));
        CHECK(reader.valid());

        ClosedBar bar;
        CHECK(reader.read(bar));
        CHECK_EQUAL(bar.open, 25_dec);
        CHECK_EQUAL(bar.high, 26_dec);
        CHECK_EQUAL(bar.low, 23_dec);
        CHECK_EQUAL(bar.close, 24_dec);
        CHECK_EQUAL(bar.volume, 77_dec);
        CHECK_EQUAL(bar.trades, 4U);
        CHECK_EQUAL(bar.start_time, tp("2026-06-26T09:00:00"));
        CHECK_EQUAL(bar.end_time, tp("2026-06-26T09:01:00"));
        CHECK(reader.read(bar));
        CHECK_EQUAL(bar.close, 21_dec);
        CHECK_EQUAL(bar.trades, 2U);
        CHECK(!reader.read(bar));
    }

    //columns are matched by name, their order in the file does not matter
    {
        HistoryOHLCReader reader(text_source(
            "close,time,volume,low,high,open\n"
            "24,2026-06-26,77,23,26,25\n"), std::chrono::seconds(86400));
        CHECK(reader.valid());
        ClosedBar bar;
        CHECK(reader.read(bar));
        CHECK_EQUAL(bar.open, 25_dec);
        CHECK_EQUAL(bar.high, 26_dec);
        CHECK_EQUAL(bar.low, 23_dec);
        CHECK_EQUAL(bar.close, 24_dec);
        CHECK_EQUAL(bar.volume, 77_dec);
    }

    //a close-only file reads as flat bars, no stale value leaks from the previous row
    {
        HistoryOHLCReader reader(text_source(
            "time,close\n"
            "2026-06-26,24\n"
            "2026-06-27,21\n"), std::chrono::seconds(86400));
        CHECK(reader.valid());
        ClosedBar bar;
        CHECK(reader.read(bar));
        CHECK_EQUAL(bar.open, 24_dec);
        CHECK_EQUAL(bar.high, 24_dec);
        CHECK_EQUAL(bar.low, 24_dec);
        CHECK_EQUAL(bar.volume, 0_dec);
        CHECK_EQUAL(bar.trades, 0U);
        CHECK(reader.read(bar));
        CHECK_EQUAL(bar.open, 21_dec);
        CHECK_EQUAL(bar.high, 21_dec);
        CHECK_EQUAL(bar.low, 21_dec);
    }

    //without a close column the reader cannot serve anything
    {
        HistoryOHLCReader reader(text_source("time,open,high,low\n"), std::chrono::seconds(60));
        CHECK(!reader.valid());
    }
    {
        HistoryOHLCReader reader(text_source("open,high,low,close\n"), std::chrono::seconds(60));
        CHECK(!reader.valid());
    }
}

static void test_trade_reader() {
    std::cout << "--- test_trade_reader" << std::endl;

    HistoryTradeReader reader(text_source(
        "time,close,volume\n"
        "2026-06-26T09:00:00,24,7\n"
        "2026-06-26T09:00:01,24.5,3\n"));
    CHECK(reader.valid());

    Trade tr;
    CHECK(reader.read(tr));
    CHECK_EQUAL(tr.price, 24_dec);
    CHECK_EQUAL(tr.size, 7_dec);
    CHECK_EQUAL(tr.time, tp("2026-06-26T09:00:00"));
    CHECK(reader.read(tr));
    CHECK_EQUAL(tr.price, 24.5_dec);
    CHECK(!reader.read(tr));

    HistoryTradeReader bad(text_source("time,volume\n"));
    CHECK(!bad.valid());
}

static void test_auction_reader() {
    std::cout << "--- test_auction_reader" << std::endl;

    HistoryAuctionReader reader(text_source(
        "time,open_price,open_volume,close_price,close_volume\n"
        "2026-06-26,25,100,24,200\n"
        "2026-06-27,0,0,21,50\n"));
    CHECK(reader.valid());

    AuctionDailyHistory a;
    CHECK(reader.read(a));
    CHECK_EQUAL(a.open_price, 25_dec);
    CHECK_EQUAL(a.open_quantity, 100_dec);
    CHECK_EQUAL(a.close_price, 24_dec);
    CHECK_EQUAL(a.close_quantity, 200_dec);
    CHECK(a.day == std::chrono::year_month_day(std::chrono::year{2026},std::chrono::month{6},std::chrono::day{26}));
    CHECK(reader.read(a));
    //a zero price means the auction did not firm
    CHECK_EQUAL(a.open_price, 0_dec);
    CHECK_EQUAL(a.close_price, 21_dec);
    CHECK(a.day == std::chrono::year_month_day(std::chrono::year{2026},std::chrono::month{6},std::chrono::day{27}));
    CHECK(!reader.read(a));

    HistoryAuctionReader bad(text_source("time,open_price,close_price\n"));
    CHECK(!bad.valid());
}

static void test_quote_reader() {
    std::cout << "--- test_quote_reader" << std::endl;

    HistoryQuoteReader reader(text_source(
        "time,ask_price,ask_volume,bid_price,bid_volume\n"
        "2026-06-26T09:00:00,24.5,10,24.0,20\n"));
    CHECK(reader.valid());

    Quote q;
    CHECK(reader.read(q));
    CHECK_EQUAL(q.ask, 24.5_dec);
    CHECK_EQUAL(q.ask_size, 10_dec);
    CHECK_EQUAL(q.bid, 24_dec);
    CHECK_EQUAL(q.bid_size, 20_dec);
    CHECK_EQUAL(q.time, tp("2026-06-26T09:00:00"));
    CHECK(!reader.read(q));

    HistoryQuoteReader bad(text_source("time,ask_price,bid_price\n"));
    CHECK(!bad.valid());
}

///the trade half of an l1 file - rows without a trade have a zero price and are skipped
static void test_l1_trade_reader() {
    std::cout << "--- test_l1_trade_reader" << std::endl;

    HistoryL1TradeReader reader(text_source(
        "time,ask_price,ask_volume,bid_price,bid_volume,price,volume\n"
        "2026-06-26T09:00:00,24.5,10,24.0,20,0,0\n"
        "2026-06-26T09:00:01,24.5,10,24.0,20,24.5,5\n"
        "2026-06-26T09:00:02,24.6,10,24.1,20,0,0\n"
        "2026-06-26T09:00:03,24.6,10,24.1,20,24.1,2\n"
        "2026-06-26T09:00:04,24.6,10,24.1,20,0,0\n"));
    CHECK(reader.valid());

    Trade tr;
    CHECK(reader.read(tr));
    CHECK_EQUAL(tr.price, 24.5_dec);
    CHECK_EQUAL(tr.size, 5_dec);
    CHECK_EQUAL(tr.time, tp("2026-06-26T09:00:01"));
    CHECK(reader.read(tr));
    CHECK_EQUAL(tr.price, 24.1_dec);
    CHECK_EQUAL(tr.size, 2_dec);
    CHECK_EQUAL(tr.time, tp("2026-06-26T09:00:03"));
    //the trailing quote-only row does not end up as a zero priced trade
    CHECK(!reader.read(tr));

    HistoryL1TradeReader bad(text_source("time,ask_price,bid_price\n"));
    CHECK(!bad.valid());
}

///the same file read as ohlc through get_stream(), from the checked-in fixture
static void test_ohlc_stream_from_file() {
    std::cout << "--- test_ohlc_stream_from_file" << std::endl;

    HistoryCSVSourceConfig cfg;
    std::filesystem::path this_file = std::source_location::current().file_name();
    std::filesystem::path test_index = this_file.parent_path() / "data" / "hist_ohlc_index.csv";
    cfg.index_file = test_index;
    cfg.interval = HistoryDataRequest::interval_day;
    cfg.type = HistoryCSVSourceConfig::ohlc;

    auto stream = EventStream<ClosedBar>::from_base( cfg.get_stream("IBM", class_hash<ClosedBar>,
                                                           {std::chrono::year{2026},std::chrono::month{1},std::chrono::day{1}},
                                                           {std::chrono::year{2026},std::chrono::month{12},std::chrono::day{12}},
                                                           {}, Timestamp::max()));

    ClosedBar x;
    bool b = coro::sync_await(stream.receive(x));
    CHECK(b);
    CHECK_EQUAL(x.open, 25_dec);
    CHECK_EQUAL(x.close, 24_dec);
    CHECK_EQUAL(x.high, 26_dec);
    CHECK_EQUAL(x.low, 23_dec);
    CHECK_EQUAL(x.volume, 77_dec);
    CHECK_EQUAL(x.end_time - x.start_time, std::chrono::hours(24));
    b = coro::sync_await(stream.receive(x));
    CHECK(b);
    CHECK_EQUAL(x.open, 23_dec);
    CHECK_EQUAL(x.high, 26_dec);
    CHECK_EQUAL(x.low, 22_dec);
    CHECK_EQUAL(x.close, 24_dec);
    CHECK_EQUAL(x.volume, 55_dec);
    b = coro::sync_await(stream.receive(x));
    CHECK(b);
    CHECK_EQUAL(x.open, 23_dec);
    CHECK_EQUAL(x.high, 23_dec);
    CHECK_EQUAL(x.low, 20_dec);
    CHECK_EQUAL(x.close, 20_dec);
    CHECK_EQUAL(x.volume, 2_dec);
    b = coro::sync_await(stream.receive(x));
    CHECK(!b);
}

int main() {
    test_string2time();
    test_parse_interval();
    test_ohlc_reader();
    test_trade_reader();
    test_auction_reader();
    test_quote_reader();
    test_l1_trade_reader();
    test_ohlc_stream_from_file();
    return 0;
}
