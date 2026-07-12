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

    struct Auction {
        struct MarketInstrumentStream {};
        ///total size
        Decimal quantity;
        ///total price
        Decimal price;
        ///size traded if final is true
        Decimal quantity_traded;
        ///imbalance
        Decimal imbalance;
        ///auction type
        AuctionType auction_type;
        ///if true, the auction is final
        bool final;       
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