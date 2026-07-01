#pragma once

#include "../stream_defs.hpp"
#include "../types.hpp"

namespace quarkbot {

struct Trade : MarketInstrumentStreamTypeItem {
    ///trade price
    Decimal price;
    ///trade volume
    Decimal size;
    ///time of execution
    std::chrono::system_clock::time_point time;
    ///taker's side - this is optional - exchange don't need to report side
    Side side  = Side::undetermined;
    Trade &view() {return *this;}
    static constexpr Type type = "trade";

    bool operator==(const Trade &) const  = default;
};

}