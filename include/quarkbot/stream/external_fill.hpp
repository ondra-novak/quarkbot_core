#pragma once

#include "../types.hpp"

namespace quarkbot {

///Streaming type - listen on fills from other sources - for example from other strategies, liquidation engine or user's manual trades.
struct ExternalFill : Fill{
    struct TradableInstrumentStream{};
};


}