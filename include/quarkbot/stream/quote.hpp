#pragma once

#include "../types.hpp"

namespace quarkbot {

    

    struct Quote  {
        struct MarketInstrumentStream {};
        Decimal bid;
        Decimal bid_size;
        Decimal ask;
        Decimal ask_size;
        std::chrono::system_clock::time_point time;

        constexpr Decimal mid() const {return (bid+ask)*0.5_dec;}
        constexpr bool both_sides() const {return bid && ask;}

        bool operator==(const Quote &) const  = default;
    };
}