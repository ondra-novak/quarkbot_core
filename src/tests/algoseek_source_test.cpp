#include "quarkbot/algoseek/algoseek_spec.hpp"
#include "check.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace quarkbot;

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
    std::cout << "All tests passed" << std::endl;
    return 0;
}
