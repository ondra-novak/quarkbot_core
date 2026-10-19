#include "check.h"
#include "quarkbot/stream/trade.hpp"
#include <array>
#include <utility>

using namespace quarkbot;

static_assert(static_cast<char>(TradeQualifier::opening_auction) == static_cast<char>(AuctionType::opening));
static_assert(static_cast<char>(TradeQualifier::closing_auction) == static_cast<char>(AuctionType::closing));
static_assert(static_cast<char>(TradeQualifier::intraday_auction) == static_cast<char>(AuctionType::intraday));
static_assert(static_cast<char>(TradeQualifier::unscheduled_auction) == static_cast<char>(AuctionType::unscheduled));

int main() {
    //existing four-member aggregate initialization still compiles and
    //produces a continuous trade
    Trade t{100_dec, 1_dec, {}, Side::buy};
    CHECK(t.qualifier == TradeQualifier::continuous);
    CHECK(!is_auction_trade(t.qualifier));

    constexpr std::array<std::pair<TradeQualifier, AuctionType>, 4> pairs{{
        {TradeQualifier::opening_auction, AuctionType::opening},
        {TradeQualifier::closing_auction, AuctionType::closing},
        {TradeQualifier::intraday_auction, AuctionType::intraday},
        {TradeQualifier::unscheduled_auction, AuctionType::unscheduled},
    }};
    for (auto [q, a]: pairs) {
        CHECK(is_auction_trade(q));
        CHECK(to_auction_type(q) == a);
        CHECK(to_trade_qualifier(a) == q);
    }
    CHECK(to_auction_type(TradeQualifier::continuous) == AuctionType::unknown);
    CHECK(to_trade_qualifier(AuctionType::unknown) == TradeQualifier::continuous);

    //the qualifier takes part in equality
    Trade a = t;
    a.qualifier = TradeQualifier::closing_auction;
    CHECK(!(a == t));
    return 0;
}
