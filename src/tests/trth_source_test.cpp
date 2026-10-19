#include "check.h"
#include "quarkbot/trth/trth_event_source.hpp"
#include "quarkbot/decimal.hpp"
#include "quarkbot/stream/auction.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include <zlib.h>

using namespace quarkbot;

///parse an ISO instant like "2026-05-18T07:00:02.719543660" into a time_point
static std::chrono::system_clock::time_point mk_utc(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::chrono::sys_time<std::chrono::nanoseconds> tp;
    in >> std::chrono::parse("%FT%T", tp);
    if (in.fail()) { std::cerr << "bad test instant: " << text << std::endl; exit(1); }
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(tp);
}

//row shapes taken from TickHistoryTimeAndSales_JYSK.CO (Nasdaq Copenhagen)
static const char *fixture =
R"(#RIC,Alias Underlying RIC,Domain,Date-Time,GMT Offset,Type,Price,Volume,Bid Price,Bid Size,Ask Price,Ask Size,Qualifiers,Trade Price Currency,Change Type,Old Value,New Value
JYSK.CO,,Market Price,2026-05-18T06:45:00.104245615Z,+2,Quote,,,890,910,890,780,,,,,
JYSK.CO,,Market Price,2026-05-18T06:45:00.104245615Z,+2,Auction,890,780,,,,,,,,,
JYSK.CO,,Market Price,2026-05-18T06:45:00.104245615Z,+2,Quote,,,,,,,"""B ""[IMB_SIDE];""O ""[IMB_ACT_TP]",,,,
JYSK.CO,,Market Price,2026-05-18T06:50:00.000000000Z,+2,Auction,891,800,,,,,,,,,
JYSK.CO,,Market Price,2026-05-18T06:50:00.000000000Z,+2,Auction,891,800,,,,,,,,,
JYSK.CO,,Market Price,2026-05-18T06:50:00.000000000Z,+2,Quote,,,,,,,"""S ""[IMB_SIDE];""O ""[IMB_ACT_TP]",,,,
JYSK.CO,,Market Price,2026-05-18T07:00:02.719543660Z,+2,Trade,891.5,33,,,,," 99[IRGCOND];Open Auction Trade[USER];""PA   ""[TR_TRD_FLG];5O-------P----[MMT_CLASS]",,,,
JYSK.CO,,Market Price,2026-05-18T07:00:02.720000000Z,+2,Trade,891.5,4,,,,," 99[IRGCOND];Open Auction Trade[USER];""PA   ""[TR_TRD_FLG];5O-------P----[MMT_CLASS]",,,,
JYSK.CO,,Market Price,2026-05-18T07:00:02.734781942Z,+2,Trade,891.5,2,,,,," 99[IRGCOND];Open Auction Trade[USER];""PA   ""[TR_TRD_FLG];5O-------PH---[MMT_CLASS]",,,,
JYSK.CO,,Market Price,2026-05-18T07:00:02.734781942Z,+2,Quote,,,890,11,,,,,,,
JYSK.CO,,Market Price,2026-05-18T07:00:02.734781942Z,+2,Quote,,,,,,,"""  ""[IMB_ACT_TP];""  ""[IMB_SIDE]",,,,
JYSK.CO,,Market Price,2026-05-18T07:00:02.894000000Z,+2,Trade,892,5,,,,," 99[IRGCOND];12-------P----[MMT_CLASS]",,,,
JYSK.CO,,Market Price,2026-05-18T07:00:29.839000000Z,+2,Trade,892,50,,,,," 99[IRGCOND];32D------P----[MMT_CLASS]",,,,
JYSK.CO,,Market Price,2026-05-18T07:06:25.439000000Z,+2,Auction,893,10,,,,,,,,,
JYSK.CO,,Market Price,2026-05-18T07:06:25.439000000Z,+2,Quote,,,,,,,"""F ""[IMB_ACT_TP]",,,,
JYSK.CO,,Market Price,2026-05-18T07:06:25.470000000Z,+2,Trade,893.25,7,,,,," AOD[IRGCOND];5P-------P----[MMT_CLASS]",,,,
JYSK.CO,,Market Price,2026-05-18T14:55:00.125000000Z,+2,Auction,895,500,,,,,,,,,
JYSK.CO,,Market Price,2026-05-18T14:55:00.125000000Z,+2,Quote,,,,,,,"""N ""[IMB_SIDE];""C ""[IMB_ACT_TP]",,,,
JYSK.CO,,Market Price,2026-05-18T14:59:43.619000000Z,+2,Trade,893,4,,,,," 99[IRGCOND];5K-------P----[MMT_CLASS]",,,,
JYSK.CO,,Market Price,2026-05-18T14:59:43.650000000Z,+2,Trade,893,10,,,,," 99[IRGCOND];5K-------P----[MMT_CLASS]",,,,
JYSK.CO,,Market Price,2026-05-18T15:00:54.970000000Z,+2,Trade,893,100,,,,," 99[IRGCOND];13-------P----[MMT_CLASS]",,,,
JYSK.CO,,Market Price,2026-05-19T06:45:00.109000000Z,+2,Auction,894,20,,,,,O[INST_PHASE],,,,
)";

static std::filesystem::path write_fixture() {
    auto path = std::filesystem::temp_directory_path() / "quarkbot_trth_source_test.csv.gz";
    gzFile f = gzopen(path.c_str(), "w");
    if (f == nullptr) { std::cerr << "cannot write " << path << std::endl; exit(1); }
    std::string_view text = fixture;
    gzwrite(f, text.data(), static_cast<unsigned int>(text.size()));
    gzclose(f);
    return path;
}

static void test_auctions() {
    std::cout << "--- test_auctions" << std::endl;
    auto path = write_fixture();
    TRTHEventSource src(path);
    std::vector<BacktestEvent> evs;
    BacktestEvent ev;
    while (src(ev)) evs.push_back(ev);
    std::filesystem::remove(path);

    CHECK_EQUAL(evs.size(), 9u);
    if (evs.size() != 9) return;

    //the locked quote of the call phase passes through
    CHECK(std::holds_alternative<Quote>(evs[0].data));

    //the indicative takes its type and imbalance side from the quote that follows it
    {
        auto &a = std::get<Auction>(evs[1].data);
        CHECK(a.auction_type == AuctionType::opening);
        CHECK(a.price == Decimal(890));
        CHECK(a.quantity == Decimal(780));
        CHECK(a.imbalance == Decimal(1));
    }
    //a duplicate Auction row with the same timestamp is merged
    {
        auto &a = std::get<Auction>(evs[2].data);
        CHECK(a.auction_type == AuctionType::opening);
        CHECK(a.price == Decimal(891));
        CHECK(a.imbalance == Decimal(-1));
        CHECK(evs[2].time == mk_utc("2026-05-18T06:50:00"));
    }
    //the prints of one uncross (MMT 5O) are one opening auction trade
    {
        auto &t = std::get<Trade>(evs[3].data);
        CHECK(t.qualifier == TradeQualifier::opening_auction);
        CHECK(t.price == Decimal::from_string("891.5"));
        CHECK(t.size == Decimal(39));
        CHECK(t.time == mk_utc("2026-05-18T07:00:02.719543660"));
        CHECK(evs[3].time == t.time);
    }
    //the row ending the run of prints is not lost
    {
        auto &q = std::get<Quote>(evs[4].data);
        CHECK(q.bid == Decimal(890));
        CHECK(evs[4].time == mk_utc("2026-05-18T07:00:02.734781942"));
    }
    //continuous trade; the dark trade (3x) is dropped
    {
        auto &t = std::get<Trade>(evs[5].data);
        CHECK(t.qualifier == TradeQualifier::continuous);
        CHECK(t.price == Decimal(892));
        CHECK(t.size == Decimal(5));
    }
    //Auction on Demand (IMB_ACT_TP F and MMT 5P) is dropped
    {
        auto &a = std::get<Auction>(evs[6].data);
        CHECK(a.auction_type == AuctionType::closing);
        CHECK(a.imbalance == Decimal(0));
    }
    //closing uncross (MMT 5K); the after-close trade (13) is dropped
    {
        auto &t = std::get<Trade>(evs[7].data);
        CHECK(t.qualifier == TradeQualifier::closing_auction);
        CHECK(t.size == Decimal(14));
        CHECK(t.time == mk_utc("2026-05-18T14:59:43.619"));
    }
    //INST_PHASE in the Auction row itself still works, also at the end of the file
    {
        auto &a = std::get<Auction>(evs[8].data);
        CHECK(a.auction_type == AuctionType::opening);
        CHECK(a.price == Decimal(894));
    }
}

int main() {
    test_auctions();
    return 0;
}
