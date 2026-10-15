#pragma once

#include "../types.hpp"
#include "auction.hpp"

namespace quarkbot {

///Trading phase a trade was executed in
/**
 * Follows the MMT level 2 "trading mode" (FIX TradingSessionSubID carries the
 * same idea). The auction values use the same characters as AuctionType.
 */
enum class TradeQualifier : char {
    ///continuous trading phase
    continuous = 0,
    opening_auction = 'O',
    closing_auction = 'C',
    intraday_auction = 'I',
    ///unscheduled auction, e.g. reopening after a halt
    unscheduled_auction = 'U',
};

struct Trade{
    struct MarketInstrumentStream {};
    ///trade price
    Decimal price;
    ///trade volume
    Decimal size;
    ///time of execution
    std::chrono::system_clock::time_point time;
    ///taker's side - this is optional - exchange don't need to report side
    Side side  = Side::undetermined;
    ///phase the trade was executed in - an auction result is a trade with an auction qualifier
    TradeQualifier qualifier = TradeQualifier::continuous;

    bool operator==(const Trade &) const  = default;
};

///true when the trade is an auction result
constexpr bool is_auction_trade(TradeQualifier q) {
    return q != TradeQualifier::continuous;
}

///auction type of an auction trade, AuctionType::unknown for a continuous trade
constexpr AuctionType to_auction_type(TradeQualifier q) {
    switch (q) {
        case TradeQualifier::opening_auction: return AuctionType::opening;
        case TradeQualifier::closing_auction: return AuctionType::closing;
        case TradeQualifier::intraday_auction: return AuctionType::intraday;
        case TradeQualifier::unscheduled_auction: return AuctionType::unscheduled;
        default: return AuctionType::unknown;
    }
}

///qualifier of a trade resulting from the given auction, continuous for AuctionType::unknown
constexpr TradeQualifier to_trade_qualifier(AuctionType t) {
    switch (t) {
        case AuctionType::opening: return TradeQualifier::opening_auction;
        case AuctionType::closing: return TradeQualifier::closing_auction;
        case AuctionType::intraday: return TradeQualifier::intraday_auction;
        case AuctionType::unscheduled: return TradeQualifier::unscheduled_auction;
        default: return TradeQualifier::continuous;
    }
}

}
