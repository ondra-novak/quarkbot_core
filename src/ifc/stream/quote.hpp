#pragma once

#include "../stream_defs.hpp"
#include "../types.hpp"

namespace quarkbot {

    

    struct Quote : MarketInstrumentStreamTypeItem {
        Decimal bid;
        Decimal bid_size;
        Decimal ask;
        Decimal ask_size;
        std::chrono::system_clock::time_point time;
        static constexpr Type type = "quote";
        constexpr Quote &view() {return *this;}

        constexpr Decimal mid() const {return (bid+ask)*0.5_dec;}

        bool operator==(const Quote &) const  = default;
    };
}