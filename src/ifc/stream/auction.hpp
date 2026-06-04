#pragma once

#include "../types.hpp"
#include "../stream_defs.hpp"
#include <chrono>

namespace quarkbot {

    enum class AuctionType : char{
        unknown = 0,
        opening = 'O',
        closing = 'C',
        intraday = 'I',
        unscheduled = 'U',
    };

    struct Auction: quarkbot::MarketInstrumentStreamTypeItem {
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

}