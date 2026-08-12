#include "quarkbot/algoseek/algoseek_spec.hpp"
#include "check.h"
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
#include "quarkbot/abstract/backtest_data_source.hpp"
#include <zlib.h>
#include <variant>

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

int main() {
    test_spec_parsing();
    test_local_time_converter();
    test_trades();
    std::cout << "All tests passed" << std::endl;
    return 0;
}
