#pragma once

#include "../types.hpp"

namespace quarkbot {




///Streaming type - listen on funding events, if applicable for the instrument
struct FundingEvent  {
    struct TradableInstrumentStream{};
    /// amount for this funding
    Decimal amount;
    /// rate,  if the funding is in different currency,
    double rate = 1.0;

};


}