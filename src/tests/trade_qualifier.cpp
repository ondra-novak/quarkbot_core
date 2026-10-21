#include "check.h"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/stream/tradestat.hpp"
#include <chrono>
#include <array>
#include <utility>

using namespace quarkbot;

static_assert(static_cast<char>(TradeQualifier::opening_auction) == static_cast<char>(AuctionType::opening));
static_assert(static_cast<char>(TradeQualifier::closing_auction) == static_cast<char>(AuctionType::closing));
static_assert(static_cast<char>(TradeQualifier::intraday_auction) == static_cast<char>(AuctionType::intraday));
static_assert(static_cast<char>(TradeQualifier::unscheduled_auction) == static_cast<char>(AuctionType::unscheduled));

///auction fields of TradeStatCounter - an auction can be detected even when its Trade is missed
static void test_trade_stat_auctions() {
    using namespace std::chrono_literals;
    std::chrono::system_clock::time_point t0{};
    auto mk = [&](Decimal price, Decimal size, std::chrono::seconds at, TradeQualifier q) {
        return Trade{price, size, t0 + at, Side::undetermined, q};
    };

    TradeStatCounter s0;
    CHECK(s0.auctions == 0);
    CHECK(s0.last_auction_type == AuctionType::unknown);

    TradeStatCounter s1 = s0.add(mk(100_dec, 5_dec, 1s, TradeQualifier::continuous));
    //a continuous trade leaves the auction fields alone
    CHECK(delta_auctions(s0, s1) == 0);
    CHECK(s1.auction_volume == 0_dec);
    CHECK(s1.last_auction_type == AuctionType::unknown);

    TradeStatCounter s2 = s1.add(mk(102_dec, 40_dec, 2s, TradeQualifier::opening_auction));
    TradeStatCounter s3 = s2.add(mk(103_dec, 3_dec, 3s, TradeQualifier::continuous));
    //sampled after a continuous trade, the opening is still visible
    CHECK(delta_auctions(s1, s3) == 1);
    CHECK(delta_auction_volume(s1, s3) == 40_dec);
    CHECK(auction_vwap(s1, s3) == 102_dec);
    CHECK(s3.last_auction_time == t0 + 2s);
    CHECK(s3.last_auction_type == AuctionType::opening);

    TradeStatCounter s4 = s3.add(mk(110_dec, 60_dec, 4s, TradeQualifier::closing_auction));
    //over both auctions: (102*40 + 110*60) / 100
    CHECK(delta_auctions(s0, s4) == 2);
    CHECK(delta_auction_volume(s0, s4) == 100_dec);
    CHECK(auction_vwap(s0, s4) == Decimal::from_string("106.8"));
    CHECK(s4.last_auction_time == t0 + 4s);
    CHECK(s4.last_auction_type == AuctionType::closing);
    CHECK(auction_vwap(s0, s1) == 0_dec);

    //the existing statistics still include the auction trades
    CHECK(delta_trades(s0, s4) == 4);
    CHECK(delta_volume(s0, s4) == 108_dec);
    CHECK(s4.last_price == 110_dec);
    CHECK(s4.weighted_price_sum == 100_dec*5_dec + 102_dec*40_dec + 103_dec*3_dec + 110_dec*60_dec);
    CHECK(s4.time == t0 + 4s);
}

int main() {
    test_trade_stat_auctions();
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
