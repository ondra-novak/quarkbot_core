#pragma once

#include "../order_defs.hpp"
#include <chrono>

namespace quarkbot {

    enum class AuctionType : char{
        unknown = 0,
        opening = 'O',
        closing = 'C',
        intraday = 'I',
        unscheduled = 'U',
    };

    ///Indicative auction data - the auction result itself is a Trade with an auction qualifier
    struct Auction {
        struct MarketInstrumentStream {};
        ///indicative executable volume
        Decimal quantity;
        ///indicative price
        Decimal price;
        ///imbalance (surplus)
        Decimal imbalance;
        ///auction type
        AuctionType auction_type = AuctionType::unknown;
        ///timestamp
        std::chrono::system_clock::time_point time;
    };

    constexpr bool is_valid_order_for_auction_type(TimeInForce tif, AuctionType at) {
        switch (tif) {
            case TimeInForce::atc: return at == AuctionType::closing;
            case TimeInForce::ato:  return at == AuctionType::opening;
            case TimeInForce::crossing: return true;
            default: return false;
        }
    }

}