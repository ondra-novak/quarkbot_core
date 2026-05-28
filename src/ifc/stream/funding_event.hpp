#pragma once

#include "../stream_defs.hpp"
#include "../types.hpp"

namespace quarkbot {




///Streaming type - listen on funding events, if applicable for the instrument
struct FundingEvent : public TradableInstrumentStreamTypeItem {
    /// amount for this funding
    Decimal amount;
    /// rate,  if the funding is in different currency,
    double rate = 1.0;

    static constexpr Type type = "funding";
    FundingEvent &view() {return *this;}
};


}