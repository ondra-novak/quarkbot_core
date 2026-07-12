#pragma once

#include "../types.hpp"

namespace quarkbot {

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

    bool operator==(const Trade &) const  = default;
};

}