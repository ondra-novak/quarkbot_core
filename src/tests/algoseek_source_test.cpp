#include "quarkbot/algoseek/algoseek_spec.hpp"
#include "check.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace quarkbot;

#include "quarkbot/algoseek/local_time_converter.hpp"
#include "quarkbot/algoseek/algoseek_data_source.hpp"
#include "quarkbot/decimal.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/stream/auction.hpp"
#include "quarkbot/abstract/backtest_data_source.hpp"
#include <zlib.h>
#include <variant>
#include <vector>

///build a local_time from Y/M/D and a time of day, for readable test data
static std::chrono::local_time<std::chrono::nanoseconds> mk_local(
        int y, unsigned m, unsigned d, int hh, int mm, int ss, long long ns) {
    using namespace std::chrono;
    return local_time<nanoseconds>{
        local_days{year{y}/month{m}/day{d}}.time_since_epoch()
        + hours{hh} + minutes{mm} + seconds{ss} + nanoseconds{ns}};
}

///parse an ISO instant like "2026-04-09 08:00:00.010833553" into a time_point
static std::chrono::system_clock::time_point mk_utc(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::chrono::sys_time<std::chrono::nanoseconds> tp;
    in >> std::chrono::parse("%F %T", tp);
    if (in.fail()) { std::cerr << "bad test instant: " << text << std::endl; exit(1); }
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(tp);
}

static void test_local_time_converter() {
    std::cout << "--- test_local_time_converter" << std::endl;

    LocalTimeConverter et(std::chrono::locate_zone("America/New_York"));

    // EDT (-04:00), nanoseconds preserved
    CHECK(et.to_sys(mk_local(2026, 4, 9, 4, 0, 0, 10833553))
            == mk_utc("2026-04-09 08:00:00.010833553"));

    // the two auction instants asserted later against the real BIPC fixture
    CHECK(et.to_sys(mk_local(2023, 6, 9, 9, 30, 0, 791480832))
            == mk_utc("2023-06-09 13:30:00.791480832"));
    CHECK(et.to_sys(mk_local(2023, 6, 9, 16, 0, 2, 164273920))
            == mk_utc("2023-06-09 20:00:02.164273920"));

    // EST (-05:00), then EDT, then EST again on the same instance:
    // exercises cache invalidation in both directions
    CHECK(et.to_sys(mk_local(2024, 1, 15, 9, 30, 0, 0)) == mk_utc("2024-01-15 14:30:00"));
    CHECK(et.to_sys(mk_local(2024, 7, 15, 9, 30, 0, 0)) == mk_utc("2024-07-15 13:30:00"));
    CHECK(et.to_sys(mk_local(2024, 1, 15, 9, 30, 0, 0)) == mk_utc("2024-01-15 14:30:00"));

    // UTC is the identity
    LocalTimeConverter utc(std::chrono::locate_zone("UTC"));
    CHECK(utc.to_sys(mk_local(2026, 4, 9, 4, 0, 0, 10833553))
            == mk_utc("2026-04-09 04:00:00.010833553"));

    // an ambiguous local time (the repeated hour of the autumn transition)
    // must resolve to the earlier of the two instants
    {
        LocalTimeConverter amb(std::chrono::locate_zone("America/New_York"));
        CHECK(amb.to_sys(mk_local(2023, 11, 5, 1, 30, 0, 0))
                == mk_utc("2023-11-05 05:30:00"));
    }

    // a nonexistent local time (the skipped hour of the spring transition)
    // resolves using the offset in effect before the transition
    {
        LocalTimeConverter gap(std::chrono::locate_zone("America/New_York"));
        CHECK(gap.to_sys(mk_local(2023, 3, 12, 2, 30, 0, 0))
                == mk_utc("2023-03-12 07:30:00"));
    }

    // a pre-epoch local time must still take the lookup path
    {
        LocalTimeConverter old(std::chrono::locate_zone("America/New_York"));
        CHECK(old.to_sys(mk_local(1969, 12, 31, 23, 59, 59, 500000000))
                == mk_utc("1970-01-01 04:59:59.500000000"));
    }
}

static void write_gz(const std::string &path, std::string_view content) {
    gzFile f = gzopen(path.c_str(), "wb");
    if (!f) { std::cerr << "Cannot open gz for write: " << path << std::endl; exit(1); }
    gzwrite(f, content.data(), static_cast<unsigned>(content.size()));
    gzclose(f);
}

static const std::string_view ALGOSEEK_HEADER =
    "Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange,Conditions\n";

static void test_trades() {
    std::cout << "--- test_trades" << std::endl;

    const std::string path = "/tmp/test_algoseek_trades.csv.gz";
    write_gz(path, std::string(ALGOSEEK_HEADER) +
        "20230609,04:00:00.010833553,TRADE,IBM,242.54,6,EDGX,80002000\n"
        "20230609,09:30:01.500000000,TRADE NB,IBM,243.10,250,NASDAQ,20002020\n");

    auto spec = parse_algoseek_spec(path + "?tzone=America/New_York");
    AlgoseekDataSource src(std::move(spec));

    BacktestEvent e1;
    CHECK(src(e1));
    CHECK_EQUAL(e1.symbol, std::string("IBM"));
    CHECK(e1.time == mk_utc("2023-06-09 08:00:00.010833553"));
    CHECK(std::holds_alternative<Trade>(e1.data));
    {
        auto &t = std::get<Trade>(e1.data);
        CHECK(t.price == Decimal::from_string("242.54"));
        CHECK(t.size == Decimal::from_string("6"));
        CHECK(t.side == Side::undetermined);
        CHECK(t.time == e1.time);
    }

    // TRADE NB is a real trade too, not a duplicate of a TRADE row
    BacktestEvent e2;
    CHECK(src(e2));
    CHECK(std::holds_alternative<Trade>(e2.data));
    CHECK(std::get<Trade>(e2.data).price == Decimal::from_string("243.10"));
    CHECK(e2.time == mk_utc("2023-06-09 13:30:01.500000000"));

    BacktestEvent e3;
    CHECK(!src(e3));
    // exhausted source keeps returning false
    CHECK(!src(e3));

    // usable as a BacktestDataSource
    {
        BacktestDataSource ds = AlgoseekDataSource(
                parse_algoseek_spec(path + "?tzone=America/New_York"));
        BacktestEvent ev;
        CHECK(ds(ev));
        CHECK(std::holds_alternative<Trade>(ev.data));
    }
}

///drain a source into a vector, for tests that care about the whole sequence
static std::vector<BacktestEvent> drain(AlgoseekDataSource &src) {
    std::vector<BacktestEvent> out;
    BacktestEvent ev;
    while (src(ev)) out.push_back(std::move(ev));
    return out;
}

static void test_auctions_and_skips() {
    std::cout << "--- test_auctions_and_skips" << std::endl;

    const std::string path = "/tmp/test_algoseek_auctions.csv.gz";
    write_gz(path, std::string(ALGOSEEK_HEADER) +
        // opening print, bit 6 (0x40)
        "20230609,09:30:00.791480832,TRADE NB,BIPC,47.58,4000,NYSE,20000040\n"
        // official open re-broadcast, bit 26 (0x04000000) - dropped
        "20230609,09:30:00.791483904,TRADE,BIPC,47.58,4000,NYSE,04000000\n"
        // ordinary trade
        "20230609,10:00:00.000000000,TRADE,BIPC,47.60,100,NYSE,00000001\n"
        // reopening print, bit 8 (0x100)
        "20230609,11:00:00.000000000,TRADE,BIPC,47.20,500,NYSE,00000100\n"
        // trade that a later row cancels; it is emitted and never taken back
        "20230609,12:00:00.000000000,TRADE,BIPC,47.70,42,NYSE,00000001\n"
        // closing print, bit 7 (0x80), also carrying official_close bit 24:
        // the auction reading must win
        "20230609,16:00:02.164273920,TRADE NB,BIPC,47.92,23455,NYSE,21000080\n"
        // quantity 0 close re-broadcast, as the real files emit at 16:10 - dropped
        "20230609,16:10:00.002849536,TRADE NB,BIPC,47.92,0,NYSE,60000000\n"
        // the cancellation row itself - dropped
        "20230609,16:30:00.000000000,TRADE CANCELLED,BIPC,47.70,42,NYSE,00000001\n"
        // unexpected event type - dropped
        "20230609,16:31:00.000000000,QUOTE BID,BIPC,47.70,42,NYSE,00000001\n");

    {
        AlgoseekDataSource src(parse_algoseek_spec(path + "?tzone=America/New_York"));
        auto evs = drain(src);
        CHECK_EQUAL(evs.size(), std::size_t(5));

        // 1: opening auction
        CHECK(std::holds_alternative<Auction>(evs[0].data));
        {
            auto &a = std::get<Auction>(evs[0].data);
            CHECK(a.auction_type == AuctionType::opening);
            CHECK(a.final);
            CHECK(a.price == Decimal::from_string("47.58"));
            CHECK(a.quantity == Decimal::from_string("4000"));
            CHECK(a.quantity_traded == a.quantity);
            CHECK(a.imbalance == Decimal(0));
            CHECK(a.time == mk_utc("2023-06-09 13:30:00.791480832"));
        }

        // 2: ordinary trade (the official open row in between was dropped)
        CHECK(std::holds_alternative<Trade>(evs[1].data));
        CHECK(std::get<Trade>(evs[1].data).price == Decimal::from_string("47.60"));

        // 3: reopening auction maps to unscheduled
        CHECK(std::holds_alternative<Auction>(evs[2].data));
        CHECK(std::get<Auction>(evs[2].data).auction_type == AuctionType::unscheduled);

        // 4: the cancelled trade survives
        CHECK(std::holds_alternative<Trade>(evs[3].data));
        CHECK(std::get<Trade>(evs[3].data).size == Decimal::from_string("42"));

        // 5: closing auction wins over the official_close bit on the same row
        CHECK(std::holds_alternative<Auction>(evs[4].data));
        {
            auto &a = std::get<Auction>(evs[4].data);
            CHECK(a.auction_type == AuctionType::closing);
            CHECK(a.final);
            CHECK(a.quantity == Decimal::from_string("23455"));
            CHECK(a.time == mk_utc("2023-06-09 20:00:02.164273920"));
        }
    }
}

static void test_exchange_filter_and_symbol() {
    std::cout << "--- test_exchange_filter_and_symbol" << std::endl;

    const std::string path = "/tmp/test_algoseek_filter.csv.gz";
    write_gz(path, std::string(ALGOSEEK_HEADER) +
        "20230609,10:00:00.000000000,TRADE,IBM,47.60,100,NASDAQ,00000001\n"
        "20230609,10:00:01.000000000,TRADE,IBM,47.61,200,ARCA,00000001\n"
        "20230609,10:00:02.000000000,TRADE,IBM,47.62,300,FINRA,00000001\n"
        "20230609,10:00:03.000000000,TRADE,IBM,47.63,400,NASDAQ,00000001\n");

    // no filter: every venue passes through
    {
        AlgoseekDataSource src(parse_algoseek_spec(path));
        CHECK_EQUAL(drain(src).size(), std::size_t(4));
    }

    // filter keeps one venue only
    {
        AlgoseekDataSource src(parse_algoseek_spec(path + "?exchange=NASDAQ"));
        auto evs = drain(src);
        CHECK_EQUAL(evs.size(), std::size_t(2));
        CHECK(std::get<Trade>(evs[0].data).price == Decimal::from_string("47.60"));
        CHECK(std::get<Trade>(evs[1].data).price == Decimal::from_string("47.63"));
        CHECK_EQUAL(evs[0].symbol, std::string("IBM"));
    }

    // symbol override replaces the Ticker column
    {
        AlgoseekDataSource src(parse_algoseek_spec(path + "?exchange=ARCA&symbol=IBM.ARCA"));
        auto evs = drain(src);
        CHECK_EQUAL(evs.size(), std::size_t(1));
        CHECK_EQUAL(evs[0].symbol, std::string("IBM.ARCA"));
    }

    // an unmatched exchange yields nothing rather than an error
    {
        AlgoseekDataSource src(parse_algoseek_spec(path + "?exchange=NASDQ"));
        CHECK_EQUAL(drain(src).size(), std::size_t(0));
    }
}

static void test_spec_parsing() {
    std::cout << "--- test_spec_parsing" << std::endl;

    // bare path: no exchange filter, UTC, no symbol override
    {
        auto s = parse_algoseek_spec("IBM.csv.gz");
        CHECK_EQUAL(s.file.string(), std::string("IBM.csv.gz"));
        CHECK(s.exchange.empty());
        CHECK(s.symbol.empty());
        CHECK(s.tz != nullptr);
        // "UTC" is a link to the canonical zone "Etc/UTC" in the IANA tzdata
        // backward-compatibility table, so locate_zone("UTC")->name() reports
        // "Etc/UTC" on every standards-conformant implementation. Compare by
        // identity against the same lookup instead of asserting a name string
        // that libstdc++ (and any implementation resolving tzdata links the
        // same way) can never produce.
        CHECK(s.tz == std::chrono::locate_zone("UTC"));
    }

    // all parameters
    {
        auto s = parse_algoseek_spec(
            "data/IBM.csv.gz?exchange=NASDAQ&tzone=America/New_York&symbol=IBM.NASDAQ");
        CHECK_EQUAL(s.file.string(), std::string("data/IBM.csv.gz"));
        CHECK_EQUAL(s.exchange, std::string("NASDAQ"));
        CHECK_EQUAL(s.symbol, std::string("IBM.NASDAQ"));
        CHECK_EQUAL(std::string(s.tz->name()), std::string("America/New_York"));
    }

    // parameter order does not matter, exchange values may contain a space
    {
        auto s = parse_algoseek_spec("IBM.csv.gz?tzone=America/New_York&exchange=BATS Y");
        CHECK_EQUAL(s.exchange, std::string("BATS Y"));
        CHECK_EQUAL(std::string(s.tz->name()), std::string("America/New_York"));
    }

    // empty query after '?' behaves like a bare path
    {
        auto s = parse_algoseek_spec("IBM.csv.gz?");
        CHECK(s.exchange.empty());
        // see identity-comparison note above regarding "UTC" being a tzdata link
        CHECK(s.tz == std::chrono::locate_zone("UTC"));
    }

    // trailing '&' is tolerated
    {
        auto s = parse_algoseek_spec("IBM.csv.gz?exchange=NYSE&");
        CHECK_EQUAL(s.exchange, std::string("NYSE"));
    }

    CHECK_EXCEPTION(std::runtime_error, parse_algoseek_spec("IBM.csv.gz?bogus=1"));
    CHECK_EXCEPTION(std::runtime_error, parse_algoseek_spec("IBM.csv.gz?exchange"));
    CHECK_EXCEPTION(std::runtime_error, parse_algoseek_spec("IBM.csv.gz?tzone=Mars/Olympus"));
    CHECK_EXCEPTION(std::runtime_error, parse_algoseek_spec("?exchange=NYSE"));

    // the error message names the offending key
    CHECK_EXCEPTION_EXPR(std::runtime_error, e,
            std::string_view(e.what()).find("bogus") != std::string_view::npos,
            parse_algoseek_spec("IBM.csv.gz?bogus=1"));
}

static void test_row_errors() {
    std::cout << "--- test_row_errors" << std::endl;

    // missing header column
    {
        const std::string path = "/tmp/test_algoseek_badheader.csv.gz";
        write_gz(path, "Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange\n"
                       "20230609,10:00:00.000000000,TRADE,IBM,47.60,100,NYSE\n");
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
                std::string_view(e.what()).find("Conditions") != std::string_view::npos,
                AlgoseekDataSource src(parse_algoseek_spec(path)));
    }

    // nine fields: a Price rewritten with a comma decimal separator shifts the
    // columns, so Conditions ends up holding the exchange name
    {
        const std::string path = "/tmp/test_algoseek_shifted.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:00.000000000,TRADE,IBM,242,54,6,EDGX,80002000\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
                std::string_view(e.what()).find("Conditions") != std::string_view::npos,
                src(ev));
    }

    // Conditions that is not hexadecimal
    {
        const std::string path = "/tmp/test_algoseek_badcond.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:00.000000000,TRADE,IBM,47.60,100,NYSE,zzzzzzzz\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }

    // an empty required field
    {
        const std::string path = "/tmp/test_algoseek_empty.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:00.000000000,TRADE,IBM,,100,NYSE,00000001\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
                std::string_view(e.what()).find("Price") != std::string_view::npos,
                src(ev));
    }

    // a malformed timestamp
    {
        const std::string path = "/tmp/test_algoseek_badtime.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:00,TRADE,IBM,47.60,100,NYSE,00000001\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }

    // a decreasing timestamp would corrupt the merged timeline
    {
        const std::string path = "/tmp/test_algoseek_unordered.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:01.000000000,TRADE,IBM,47.60,100,NYSE,00000001\n"
                "20230609,10:00:00.000000000,TRADE,IBM,47.61,100,NYSE,00000001\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK_EXCEPTION(std::runtime_error, src(ev));
    }

    // a changing Ticker would let a symbol override merge two instruments
    {
        const std::string path = "/tmp/test_algoseek_twotickers.csv.gz";
        write_gz(path, std::string(ALGOSEEK_HEADER) +
                "20230609,10:00:00.000000000,TRADE,IBM,47.60,100,NYSE,00000001\n"
                "20230609,10:00:01.000000000,TRADE,MSFT,47.61,100,NYSE,00000001\n");
        AlgoseekDataSource src(parse_algoseek_spec(path));
        BacktestEvent ev;
        CHECK(src(ev));
        CHECK_EXCEPTION_EXPR(std::runtime_error, e,
                std::string_view(e.what()).find("MSFT") != std::string_view::npos,
                src(ev));
    }

    // a nonexistent file fails when the source is constructed
    CHECK_EXCEPTION(std::runtime_error,
            AlgoseekDataSource src(parse_algoseek_spec("/tmp/does_not_exist_algoseek.csv.gz")));
}

///count emitted events by kind
struct EventCounts {
    std::size_t total = 0;
    std::size_t trades = 0;
    std::size_t opening = 0;
    std::size_t closing = 0;
    std::size_t unscheduled = 0;
};

static EventCounts count_events(const std::string &spec) {
    AlgoseekDataSource src(parse_algoseek_spec(spec));
    EventCounts c;
    for (const auto &ev: drain(src)) {
        ++c.total;
        if (std::holds_alternative<Trade>(ev.data)) {
            ++c.trades;
        } else if (std::holds_alternative<Auction>(ev.data)) {
            switch (std::get<Auction>(ev.data).auction_type) {
                case AuctionType::opening: ++c.opening; break;
                case AuctionType::closing: ++c.closing; break;
                case AuctionType::unscheduled: ++c.unscheduled; break;
                default: break;
            }
        }
    }
    return c;
}

static void test_real_files() {
    std::cout << "--- test_real_files" << std::endl;

    const std::string dhil = std::string(TEST_DATA_PATH) + "/20240418_NASDAQ_DHIL.csv.gz";
    const std::string bipc = std::string(TEST_DATA_PATH) + "/20230609_BIPC.csv.gz";
    //the same parameter as the only one, and appended to another
    const std::string tz_only = "?tzone=America/New_York";
    const std::string tz_more = "&tzone=America/New_York";

    // DHIL, 594 data rows: 588 trades + 2 auctions emitted, 4 official prints dropped
    {
        auto c = count_events(dhil + tz_only);
        CHECK_EQUAL(c.total, std::size_t(590));
        CHECK_EQUAL(c.trades, std::size_t(588));
        CHECK_EQUAL(c.opening, std::size_t(1));
        CHECK_EQUAL(c.closing, std::size_t(1));
        CHECK_EQUAL(c.unscheduled, std::size_t(0));
    }
    // restricted to NASDAQ: 321 rows belong to other venues
    {
        auto c = count_events(dhil + "?exchange=NASDAQ" + tz_more);
        CHECK_EQUAL(c.total, std::size_t(271));
        CHECK_EQUAL(c.trades, std::size_t(269));
        CHECK_EQUAL(c.opening, std::size_t(1));
        CHECK_EQUAL(c.closing, std::size_t(1));
    }

    // BIPC, 5627 data rows: 9 official prints and 3 zero quantity rows dropped
    {
        auto c = count_events(bipc + tz_only);
        CHECK_EQUAL(c.total, std::size_t(5615));
        CHECK_EQUAL(c.trades, std::size_t(5613));
        CHECK_EQUAL(c.opening, std::size_t(1));
        CHECK_EQUAL(c.closing, std::size_t(1));
    }
    // restricted to NYSE: 4737 rows belong to other venues, 5 official prints
    // and the 3 zero quantity rows remain on NYSE and are dropped
    {
        auto c = count_events(bipc + "?exchange=NYSE" + tz_more);
        CHECK_EQUAL(c.total, std::size_t(882));
        CHECK_EQUAL(c.trades, std::size_t(880));
        CHECK_EQUAL(c.opening, std::size_t(1));
        CHECK_EQUAL(c.closing, std::size_t(1));
    }

    // the auctions carry the expected values, converted from Eastern to UTC.
    // The closing print 47.92 x 23455 appears four more times in this file as an
    // official close, the last at 19:00; exactly one closing auction must survive.
    {
        AlgoseekDataSource src(parse_algoseek_spec(bipc + "?exchange=NYSE" + tz_more));
        const Auction *opening = nullptr;
        const Auction *closing = nullptr;
        auto evs = drain(src);
        for (const auto &ev: evs) {
            if (!std::holds_alternative<Auction>(ev.data)) continue;
            const auto &a = std::get<Auction>(ev.data);
            if (a.auction_type == AuctionType::opening) opening = &a;
            if (a.auction_type == AuctionType::closing) closing = &a;
        }
        CHECK(opening != nullptr);
        CHECK(opening->price == Decimal::from_string("47.58"));
        CHECK(opening->quantity == Decimal::from_string("4000"));
        CHECK(opening->time == mk_utc("2023-06-09 13:30:00.791480832"));
        CHECK(closing != nullptr);
        CHECK(closing->price == Decimal::from_string("47.92"));
        CHECK(closing->quantity == Decimal::from_string("23455"));
        CHECK(closing->time == mk_utc("2023-06-09 20:00:02.164273920"));

        // the real files come out ordered, which MergedDataSource depends on
        CHECK(std::is_sorted(evs.begin(), evs.end(),
                [](const BacktestEvent &a, const BacktestEvent &b){
                    return a.time < b.time; }));
    }
}

int main() {
    test_spec_parsing();
    test_local_time_converter();
    test_trades();
    test_auctions_and_skips();
    test_exchange_filter_and_symbol();
    test_row_errors();
    test_real_files();
    std::cout << "All tests passed" << std::endl;
    return 0;
}
