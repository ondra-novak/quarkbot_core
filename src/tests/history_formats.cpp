#include "../quarkbot/csv_history/formats.hpp"
#include "basic_coro/sync_await.hpp"
#include "quarkbot/csv_history/history_csv_source.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/hash/class_hash.hpp"
#include "quarkbot/stream/closedbar.hpp"
#include "quarkbot/timestamp.hpp"
#include "check.h"
#include <chrono>
#include <source_location>

template class quarkbot::HistoryOHLCReader<int (*)()>;
template class quarkbot::HistoryTradeReader<int (*)()>;
template class quarkbot::HistoryAuctionReader<int (*)()>;



int main() {
    quarkbot::HistoryCSVSourceConfig cfg;
    std::filesystem::path this_file = std::source_location::current().file_name();
    std::filesystem::path test_index = this_file.parent_path() / "data" / "hist_ohlc_index.csv";
    cfg.index_file = test_index;
    cfg.interval = 60;
    cfg.type = quarkbot::HistoryCSVSourceConfig::ohlc;

    auto stream = quarkbot::EventStream<quarkbot::ClosedBar>::from_base( cfg.get_stream("IBM", class_hash<quarkbot::ClosedBar>, {std::chrono::year{2026},std::chrono::month{1},std::chrono::day{1}},
                                                           {std::chrono::year{2026},std::chrono::month{12},std::chrono::day{12}}, 
                                                           {}, quarkbot::Timestamp::max()));

    quarkbot::ClosedBar x;
    bool b = coro::sync_await(stream.receive(x));
    CHECK(b);
    CHECK_EQUAL(x.open, 25_dec);
    CHECK_EQUAL(x.close, 24_dec);
    CHECK_EQUAL(x.high, 26_dec);
    CHECK_EQUAL(x.low, 23_dec);
    CHECK_EQUAL(x.volume, 77_dec);
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