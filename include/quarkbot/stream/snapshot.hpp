#pragma once

#include "quarkbot/stream/quote.hpp"
#include <chrono>
namespace quarkbot {

    struct Snapshot: Quote {
        Decimal last_price;
        std::chrono::system_clock::time_point last_price_timestamp;
    };

}