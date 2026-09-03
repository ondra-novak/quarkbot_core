#include "../quarkbot/csv_history/formats.hpp"
#include "quarkbot/csv_history/history_csv_source.hpp"

#include "basic_coro/sync_await.hpp"
#include "check.h"
#include "quarkbot/abstract/imarket_instrument.hpp"
#include "quarkbot/backtest/config_datasource.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/hash/class_hash.hpp"
#include "quarkbot/stream/closedbar.hpp"
#include "quarkbot/stream/history.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/timestamp.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <zlib.h>

using namespace quarkbot;

namespace {

std::filesystem::path test_dir() {
    auto dir = std::filesystem::temp_directory_path() / "qb_csv_history_test";
    std::filesystem::create_directories(dir);
    return dir;
}

void write_text(const std::filesystem::path &path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary|std::ios::trunc);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!f) {std::cerr << "Cannot write " << path << std::endl; exit(1);}
}

void write_gz(const std::filesystem::path &path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    gzFile f = gzopen(path.string().c_str(), "wb");
    if (!f) {std::cerr << "Cannot write " << path << std::endl; exit(1);}
    gzwrite(f, content.data(), static_cast<unsigned>(content.size()));
    gzclose(f);
}

constexpr std::chrono::year_month_day ymd(int y, unsigned m, unsigned d) {
    return {std::chrono::year{y}, std::chrono::month{m}, std::chrono::day{d}};
}

Timestamp at(std::string_view iso) {
    return string2time(std::string(iso));
}

///reads the whole stream into a vector; an absent stream (nullptr) yields an empty vector
template<typename T>
std::vector<T> drain(const std::shared_ptr<IEventStreamBase> &base) {
    std::vector<T> out;
    if (!base) return out;
    auto stream = EventStream<T>::from_base(base);
    T v;
    while (coro::sync_await(stream.receive(v))) out.push_back(v);
    return out;
}

///the least an instrument has to be for create_csv_history_source() to resolve it
class MockInstrument: public IMarketInstrument {
public:
    MockInstrument(std::string name) {_info.name = std::move(name);}
    PExchange get_exchange() const override {return {};}
    const Info &get_info() const override {return _info;}
    PTradableInstrument create_tradable_instrument(PAccount) override {return {};}
    PHistoryAdapter get_history() override {return {};}
    awaitable<bool> receive_snapshot(Snapshot &, std::stop_token = {}) override {return false;}
    std::shared_ptr<IEventStreamBase> subscribe_stream(std::size_t, const void *) override {return {};}
protected:
    Info _info;
};

const std::string ohlc_ibm =
    "time,open,high,low,close,volume\n"
    "2026-06-25,20,21,19,20,10\n"
    "2026-06-26,25,26,23,24,77\n"
    "2026-06-27,23,26,22,24,55\n"
    "2026-06-28,23,23,20,20,2\n";

const std::string l1_ibm =
    "time,ask_price,ask_volume,bid_price,bid_volume,price,volume\n"
    "2026-06-26T09:00:00,24.5,10,24.0,20,0,0\n"
    "2026-06-26T09:00:01,24.5,10,24.0,20,24.5,5\n"
    "2026-06-26T09:00:02,24.6,11,24.1,21,0,0\n"
    "2026-06-26T09:00:03,24.6,11,24.1,21,24.1,2\n";

///builds a source over one data file, written under a subdirectory of the index
HistoryCSVSourceConfig make_source(const std::string &tag, const std::string &data_file,
        std::string_view data, HistoryCSVSourceConfig::Type type,
        HistoryDataRequest::Interval interval, bool compressed = false) {
    auto dir = test_dir() / tag;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    if (compressed) write_gz(dir/"files"/data_file, data); else write_text(dir/"files"/data_file, data);
    write_text(dir/"index.csv", "symbol,file\nIBM,files/" + data_file + "\n");
    HistoryCSVSourceConfig cfg;
    cfg.index_file = dir/"index.csv";
    cfg.type = type;
    cfg.interval = interval;
    return cfg;
}

}

///the index maps a symbol to a file resolved relative to the index itself, for .csv and .csv.gz alike
static void test_index_lookup() {
    std::cout << "--- test_index_lookup" << std::endl;

    for (bool compressed: {false, true}) {
        auto cfg = make_source("index_lookup", compressed?"ibm.csv.gz":"ibm.csv", ohlc_ibm,
                HistoryCSVSourceConfig::ohlc, HistoryDataRequest::interval_day, compressed);

        auto bars = drain<ClosedBar>(cfg.get_stream("IBM", class_hash<ClosedBar>,
                ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
        CHECK_EQUAL(bars.size(), 4U);
        CHECK_EQUAL(bars.front().close, 20_dec);
        CHECK_EQUAL(bars.back().close, 20_dec);
        CHECK_EQUAL(bars[1].close, 24_dec);

        //a symbol which is not in the index simply has no history
        CHECK(!cfg.get_stream("AAPL", class_hash<ClosedBar>,
                ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
    }
}

///a source of one type does not answer a request for another type, and does not open the file for it
static void test_type_match() {
    std::cout << "--- test_type_match" << std::endl;

    auto cfg = make_source("type_match", "ibm.csv", ohlc_ibm,
            HistoryCSVSourceConfig::ohlc, HistoryDataRequest::interval_day);

    CHECK(cfg.get_stream("IBM", class_hash<ClosedBar>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
    CHECK(!cfg.get_stream("IBM", class_hash<Quote>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
    CHECK(!cfg.get_stream("IBM", class_hash<Trade>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
    CHECK(!cfg.get_stream("IBM", class_hash<AuctionDailyHistory>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));

    //an ohlc request against a file which has no close column reports no data
    auto bad = make_source("type_match_bad", "ibm.csv", "time,volume\n2026-06-25,10\n",
            HistoryCSVSourceConfig::ohlc, HistoryDataRequest::interval_day);
    CHECK(!bad.get_stream("IBM", class_hash<ClosedBar>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
}

///start_date, end_date and start_time clip the stream on both ends
static void test_date_range() {
    std::cout << "--- test_date_range" << std::endl;

    auto cfg = make_source("date_range", "ibm.csv", ohlc_ibm,
            HistoryCSVSourceConfig::ohlc, HistoryDataRequest::interval_day);

    //end_date is inclusive - its whole day is reported
    auto bars = drain<ClosedBar>(cfg.get_stream("IBM", class_hash<ClosedBar>,
            ymd(2026,6,26), ymd(2026,6,27), {}, Timestamp::max()));
    CHECK_EQUAL(bars.size(), 2U);
    CHECK_EQUAL(bars.front().start_time, at("2026-06-26"));
    CHECK_EQUAL(bars.back().start_time, at("2026-06-27"));

    //a range which ends before the file starts is empty, not an error
    bars = drain<ClosedBar>(cfg.get_stream("IBM", class_hash<ClosedBar>,
            ymd(2026,1,1), ymd(2026,1,31), {}, Timestamp::max()));
    CHECK_EQUAL(bars.size(), 0U);

    //start_time moves the beginning past the record of start_date
    std::chrono::hh_mm_ss<std::chrono::seconds> noon{std::chrono::hours(12)};
    bars = drain<ClosedBar>(cfg.get_stream("IBM", class_hash<ClosedBar>,
            ymd(2026,6,26), ymd(2026,6,28), noon, Timestamp::max()));
    CHECK_EQUAL(bars.size(), 2U);
    CHECK_EQUAL(bars.front().start_time, at("2026-06-27"));
}

///the simulation time cuts the stream, so a backtest never reads its own future
static void test_backtest_cut_time() {
    std::cout << "--- test_backtest_cut_time" << std::endl;

    auto cfg = make_source("cut_time", "ibm.csv", ohlc_ibm,
            HistoryCSVSourceConfig::ohlc, HistoryDataRequest::interval_day);

    //the simulation stands in the middle of 2026-06-27, that day's bar is still the future
    auto bars = drain<ClosedBar>(cfg.get_stream("IBM", class_hash<ClosedBar>,
            ymd(2026,6,25), ymd(2026,6,28), {}, at("2026-06-27T12:00:00")));
    CHECK_EQUAL(bars.size(), 2U);
    CHECK_EQUAL(bars.back().start_time, at("2026-06-26"));

    //the cut wins over end_date only when it is the earlier of the two
    bars = drain<ClosedBar>(cfg.get_stream("IBM", class_hash<ClosedBar>,
            ymd(2026,6,25), ymd(2026,6,26), {}, at("2026-06-30T00:00:00")));
    CHECK_EQUAL(bars.size(), 2U);
    CHECK_EQUAL(bars.back().start_time, at("2026-06-26"));
}

///one l1 file answers both a Quote and a Trade request
static void test_l1_serves_quote_and_trade() {
    std::cout << "--- test_l1_serves_quote_and_trade" << std::endl;

    auto cfg = make_source("l1", "ibm.csv", l1_ibm,
            HistoryCSVSourceConfig::l1, HistoryDataRequest::interval_undefined);

    auto quotes = drain<Quote>(cfg.get_stream("IBM", class_hash<Quote>,
            ymd(2026,6,26), ymd(2026,6,26), {}, Timestamp::max()));
    CHECK_EQUAL(quotes.size(), 4U);
    CHECK_EQUAL(quotes.front().ask, 24.5_dec);
    CHECK_EQUAL(quotes.front().bid, 24_dec);
    CHECK_EQUAL(quotes.back().ask_size, 11_dec);

    //only the rows which really traded become trades
    auto trades = drain<Trade>(cfg.get_stream("IBM", class_hash<Trade>,
            ymd(2026,6,26), ymd(2026,6,26), {}, Timestamp::max()));
    CHECK_EQUAL(trades.size(), 2U);
    CHECK_EQUAL(trades.front().price, 24.5_dec);
    CHECK_EQUAL(trades.front().size, 5_dec);
    CHECK_EQUAL(trades.back().price, 24.1_dec);

    //an l1 source still serves nothing else
    CHECK(!cfg.get_stream("IBM", class_hash<ClosedBar>, ymd(2026,6,26), ymd(2026,6,26), {}, Timestamp::max()));
}

static void test_quote_and_close_sources() {
    std::cout << "--- test_quote_and_close_sources" << std::endl;

    auto quote_cfg = make_source("quote", "ibm.csv",
        "time,ask_price,ask_volume,bid_price,bid_volume\n"
        "2026-06-26T09:00:00,24.5,10,24.0,20\n"
        "2026-06-26T09:00:01,24.6,11,24.1,21\n",
        HistoryCSVSourceConfig::quote, HistoryDataRequest::interval_undefined);
    auto quotes = drain<Quote>(quote_cfg.get_stream("IBM", class_hash<Quote>,
            ymd(2026,6,26), ymd(2026,6,26), {}, Timestamp::max()));
    CHECK_EQUAL(quotes.size(), 2U);
    CHECK_EQUAL(quotes.back().bid, 24.1_dec);
    CHECK(!quote_cfg.get_stream("IBM", class_hash<Trade>, ymd(2026,6,26), ymd(2026,6,26), {}, Timestamp::max()));

    auto close_cfg = make_source("close", "ibm.csv",
        "time,close,volume\n"
        "2026-06-26T09:00:00,24,7\n"
        "2026-06-26T09:00:01,24.5,3\n",
        HistoryCSVSourceConfig::close, HistoryDataRequest::interval_undefined);
    auto trades = drain<Trade>(close_cfg.get_stream("IBM", class_hash<Trade>,
            ymd(2026,6,26), ymd(2026,6,26), {}, Timestamp::max()));
    CHECK_EQUAL(trades.size(), 2U);
    CHECK_EQUAL(trades.back().price, 24.5_dec);
}

///an auction record belongs to its day, and is cut by a simulation time inside that day
static void test_auction_source() {
    std::cout << "--- test_auction_source" << std::endl;

    auto cfg = make_source("auction", "ibm.csv",
        "time,open_price,open_volume,close_price,close_volume\n"
        "2026-06-26,25,100,24,200\n"
        "2026-06-27,23,110,24,210\n"
        "2026-06-28,0,0,20,220\n",
        HistoryCSVSourceConfig::auction, HistoryDataRequest::interval_undefined);

    auto all = drain<AuctionDailyHistory>(cfg.get_stream("IBM", class_hash<AuctionDailyHistory>,
            ymd(2026,6,26), ymd(2026,6,28), {}, Timestamp::max()));
    CHECK_EQUAL(all.size(), 3U);
    CHECK_EQUAL(all.front().open_price, 25_dec);
    CHECK_EQUAL(all.back().close_quantity, 220_dec);
    CHECK(all.back().day == ymd(2026,6,28));

    //an auction is reported at noon of its day, so a morning simulation time excludes it
    auto morning = drain<AuctionDailyHistory>(cfg.get_stream("IBM", class_hash<AuctionDailyHistory>,
            ymd(2026,6,26), ymd(2026,6,28), {}, at("2026-06-28T09:00:00")));
    CHECK_EQUAL(morning.size(), 2U);
    CHECK(morning.back().day == ymd(2026,6,27));
}

///[symbol-mapping] renames the vendor's symbols of the index, in both modes
static void test_symbology_translate() {
    std::cout << "--- test_symbology_translate" << std::endl;

    auto dir = test_dir() / "symbology";
    std::filesystem::remove_all(dir);
    write_text(dir/"ibm.csv", ohlc_ibm);
    write_text(dir/"aapl.csv", ohlc_ibm);
    write_text(dir/"index.csv", "symbol,file\nIBM,ibm.csv\nAAPL,aapl.csv\n");

    std::unordered_map<std::string, std::string> map{{"IBM","IBM.N"}};

    {
        HistoryCSVSourceConfig cfg;
        cfg.index_file = dir/"index.csv";
        cfg.type = HistoryCSVSourceConfig::ohlc;
        cfg.interval = HistoryDataRequest::interval_day;
        cfg.set_symbology_map_ignore_missing(map);
        //the mapped symbol is served under its new name, the unmapped one keeps its own
        CHECK(cfg.get_stream("IBM.N", class_hash<ClosedBar>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
        CHECK(!cfg.get_stream("IBM", class_hash<ClosedBar>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
        CHECK(cfg.get_stream("AAPL", class_hash<ClosedBar>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
    }
    {
        HistoryCSVSourceConfig cfg;
        cfg.index_file = dir/"index.csv";
        cfg.type = HistoryCSVSourceConfig::ohlc;
        cfg.interval = HistoryDataRequest::interval_day;
        cfg.set_symbology_map_remove_missing(map);
        //an unmapped symbol is dropped from the index entirely
        CHECK(cfg.get_stream("IBM.N", class_hash<ClosedBar>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
        CHECK(!cfg.get_stream("AAPL", class_hash<ClosedBar>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
    }
}

///a broken index or a missing data file is reported, not silently ignored
static void test_error_reporting() {
    std::cout << "--- test_error_reporting" << std::endl;

    auto dir = test_dir() / "errors";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    {
        HistoryCSVSourceConfig cfg;
        cfg.index_file = dir/"missing_index.csv";
        cfg.type = HistoryCSVSourceConfig::ohlc;
        CHECK_EXCEPTION(std::runtime_error,
            cfg.get_stream("IBM", class_hash<ClosedBar>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
    }
    {
        write_text(dir/"bad_index.csv", "ticker,path\nIBM,ibm.csv\n");
        HistoryCSVSourceConfig cfg;
        cfg.index_file = dir/"bad_index.csv";
        cfg.type = HistoryCSVSourceConfig::ohlc;
        CHECK_EXCEPTION(std::runtime_error,
            cfg.get_stream("IBM", class_hash<ClosedBar>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
    }
    {
        //the index names a file which is not there
        write_text(dir/"index.csv", "symbol,file\nIBM,nowhere.csv\n");
        HistoryCSVSourceConfig cfg;
        cfg.index_file = dir/"index.csv";
        cfg.type = HistoryCSVSourceConfig::ohlc;
        CHECK_EXCEPTION(std::runtime_error,
            cfg.get_stream("IBM", class_hash<ClosedBar>, ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max()));
    }
    {
        //an unsorted file would be truncated at the first step back, report it instead
        auto cfg = make_source("unsorted", "ibm.csv",
            "time,close\n"
            "2026-06-26,24\n"
            "2026-06-25,20\n",
            HistoryCSVSourceConfig::ohlc, HistoryDataRequest::interval_day);
        CHECK_EXCEPTION(std::runtime_error,
            drain<ClosedBar>(cfg.get_stream("IBM", class_hash<ClosedBar>,
                ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max())));
    }
    {
        //a malformed timestamp is an error, not a record at the epoch
        auto cfg = make_source("badtime", "ibm.csv",
            "time,close\n"
            "yesterday,24\n",
            HistoryCSVSourceConfig::ohlc, HistoryDataRequest::interval_day);
        CHECK_EXCEPTION(std::runtime_error,
            drain<ClosedBar>(cfg.get_stream("IBM", class_hash<ClosedBar>,
                ymd(2026,6,25), ymd(2026,6,28), {}, Timestamp::max())));
    }
}

///the BacktestHistorySource wrapper resolves the symbol from the instrument and matches the interval
static void test_backtest_history_source() {
    std::cout << "--- test_backtest_history_source" << std::endl;

    auto cfg = make_source("wrapper", "ibm.csv", ohlc_ibm,
            HistoryCSVSourceConfig::ohlc, HistoryDataRequest::interval_day);
    auto source = create_csv_history_source(cfg);

    PMarketInstrument ibm = std::make_shared<MockInstrument>("IBM");
    PMarketInstrument aapl = std::make_shared<MockInstrument>("AAPL");

    HistoryDataRequest req;
    req.start_date = ymd(2026,6,25);
    req.end_date = ymd(2026,6,28);
    req.interval = HistoryDataRequest::interval_day;

    auto bars = drain<ClosedBar>(source(ibm, class_hash<ClosedBar>, req, Timestamp::max()));
    CHECK_EQUAL(bars.size(), 4U);

    //an instrument with no row in the index, and no instrument at all
    CHECK(!source(aapl, class_hash<ClosedBar>, req, Timestamp::max()));
    CHECK(!source({}, class_hash<ClosedBar>, req, Timestamp::max()));

    //a daily source does not answer a request for another interval
    req.interval = HistoryDataRequest::interval_minute;
    CHECK(!source(ibm, class_hash<ClosedBar>, req, Timestamp::max()));
    req.interval = HistoryDataRequest::interval_undefined;
    CHECK(!source(ibm, class_hash<ClosedBar>, req, Timestamp::max()));
}

namespace {

///a one row algoseek export, so that a config has the data source it requires
void write_algoseek(const std::filesystem::path &path) {
    write_gz(path,
        "Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange,Conditions\n"
        "20230609,10:00:00.000000000,TRADE,IBM,47.60,100,NASDAQ,00000001\n");
}

}

///the [history] section of a configuration builds a working source
static void test_config_section() {
    std::cout << "--- test_config_section" << std::endl;

    auto dir = test_dir() / "config";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_algoseek(dir/"IBM.csv.gz");
    write_text(dir/"daily"/"ibm.csv", ohlc_ibm);
    write_text(dir/"daily"/"index.csv", "symbol,file\nIBM,ibm.csv\n");

    {
        std::ofstream ini(dir/"backtest.ini");
        ini << "[data-source]\n"
               "algoseek=IBM.csv.gz\n"
               "[history]\n"
               "type=ohlc\n"
               "interval=d\n"
               "index=daily/index.csv\n";
    }

    auto [ds, hist] = configure_datasources(dir/"backtest.ini");
    CHECK(!!hist);

    PMarketInstrument ibm = std::make_shared<MockInstrument>("IBM");
    HistoryDataRequest req;
    req.start_date = ymd(2026,6,25);
    req.end_date = ymd(2026,6,28);
    req.interval = HistoryDataRequest::interval_day;

    auto bars = drain<ClosedBar>(hist(ibm, class_hash<ClosedBar>, req, Timestamp::max()));
    CHECK_EQUAL(bars.size(), 4U);
    CHECK_EQUAL(bars.back().close, 20_dec);

    //`interval=d` is the same interval the strategy asks for as interval_day
    req.interval = HistoryDataRequest::interval_hour;
    CHECK(!hist(ibm, class_hash<ClosedBar>, req, Timestamp::max()));
}

///[symbol-mapping] applies to the history index as well as to the replayed events
static void test_config_symbol_mapping() {
    std::cout << "--- test_config_symbol_mapping" << std::endl;

    auto dir = test_dir() / "config_mapping";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_algoseek(dir/"IBM.csv.gz");
    write_text(dir/"daily"/"ibm.csv", ohlc_ibm);
    write_text(dir/"daily"/"index.csv", "symbol,file\nIBM,ibm.csv\n");

    {
        std::ofstream ini(dir/"backtest.ini");
        ini << "[data-source]\n"
               "algoseek=IBM.csv.gz\n"
               "[symbol-mapping]\n"
               "IBM=>IBM.N\n"
               "[history]\n"
               "type=ohlc\n"
               "interval=d\n"
               "index=daily/index.csv\n";
    }

    auto [ds, hist] = configure_datasources(dir/"backtest.ini");

    HistoryDataRequest req;
    req.start_date = ymd(2026,6,25);
    req.end_date = ymd(2026,6,28);
    req.interval = HistoryDataRequest::interval_day;

    PMarketInstrument renamed = std::make_shared<MockInstrument>("IBM.N");
    PMarketInstrument original = std::make_shared<MockInstrument>("IBM");
    CHECK_EQUAL(drain<ClosedBar>(hist(renamed, class_hash<ClosedBar>, req, Timestamp::max())).size(), 4U);
    CHECK(!hist(original, class_hash<ClosedBar>, req, Timestamp::max()));
}

///an include= pulls in a second [history] block, each file declares at most one
static void test_config_include() {
    std::cout << "--- test_config_include" << std::endl;

    auto dir = test_dir() / "config_include";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_algoseek(dir/"IBM.csv.gz");
    write_text(dir/"daily"/"ibm.csv", ohlc_ibm);
    write_text(dir/"daily"/"index.csv", "symbol,file\nIBM,ibm.csv\n");
    write_text(dir/"auct"/"ibm.csv",
        "time,open_price,open_volume,close_price,close_volume\n"
        "2026-06-26,25,100,24,200\n");
    write_text(dir/"auct"/"index.csv", "symbol,file\nIBM,ibm.csv\n");

    {
        std::ofstream ini(dir/"auction.ini");
        ini << "[history]\n"
               "type=auction\n"
               "index=auct/index.csv\n";
    }
    {
        std::ofstream ini(dir/"backtest.ini");
        ini << "include=auction.ini\n"
               "[data-source]\n"
               "algoseek=IBM.csv.gz\n"
               "[history]\n"
               "type=ohlc\n"
               "interval=d\n"
               "index=daily/index.csv\n";
    }

    auto [ds, hist] = configure_datasources(dir/"backtest.ini");
    PMarketInstrument ibm = std::make_shared<MockInstrument>("IBM");

    HistoryDataRequest bars_req;
    bars_req.start_date = ymd(2026,6,25);
    bars_req.end_date = ymd(2026,6,28);
    bars_req.interval = HistoryDataRequest::interval_day;
    CHECK_EQUAL(drain<ClosedBar>(hist(ibm, class_hash<ClosedBar>, bars_req, Timestamp::max())).size(), 4U);

    //the auction source declares no interval, so its request must not declare one either
    HistoryDataRequest auct_req;
    auct_req.start_date = ymd(2026,6,25);
    auct_req.end_date = ymd(2026,6,28);
    CHECK_EQUAL(drain<AuctionDailyHistory>(hist(ibm, class_hash<AuctionDailyHistory>, auct_req, Timestamp::max())).size(), 1U);
}

///a mistake in the [history] section is reported when the configuration is read
static void test_config_errors() {
    std::cout << "--- test_config_errors" << std::endl;

    auto dir = test_dir() / "config_errors";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_algoseek(dir/"IBM.csv.gz");
    write_text(dir/"daily"/"index.csv", "symbol,file\nIBM,ibm.csv\n");
    write_text(dir/"daily"/"ibm.csv", ohlc_ibm);

    auto write_ini = [&](const char *name, std::string_view history) {
        std::ofstream ini(dir/name);
        ini << "[data-source]\n"
               "algoseek=IBM.csv.gz\n"
               "[history]\n" << history;
        return dir/name;
    };

    CHECK_EXCEPTION(std::runtime_error,
        configure_datasources(write_ini("badtype.ini", "type=candles\nindex=daily/index.csv\n")));
    CHECK_EXCEPTION(std::runtime_error,
        configure_datasources(write_ini("badinterval.ini", "type=ohlc\ninterval=fortnight\nindex=daily/index.csv\n")));
    CHECK_EXCEPTION(std::runtime_error,
        configure_datasources(write_ini("badkey.ini", "type=ohlc\ntimezone=UTC\nindex=daily/index.csv\n")));
    CHECK_EXCEPTION(std::runtime_error,
        configure_datasources(write_ini("noindex.ini", "type=ohlc\ninterval=d\n")));
    CHECK_EXCEPTION(std::runtime_error,
        configure_datasources(write_ini("notype.ini", "interval=d\nindex=daily/index.csv\n")));
    CHECK_EXCEPTION(std::runtime_error,
        configure_datasources(write_ini("missingindex.ini", "type=ohlc\nindex=daily/nowhere.csv\n")));
}

int main() {
    test_index_lookup();
    test_type_match();
    test_date_range();
    test_backtest_cut_time();
    test_l1_serves_quote_and_trade();
    test_quote_and_close_sources();
    test_auction_source();
    test_symbology_translate();
    test_error_reporting();
    test_backtest_history_source();
    test_config_section();
    test_config_symbol_mapping();
    test_config_include();
    test_config_errors();
    return 0;
}
