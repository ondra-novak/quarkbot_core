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
    Quote &view() {return *this;}

    bool operator==(const Quote &) const  = default;
};
}