#pragma once

#include "../types.hpp"
#include "../stream_defs.hpp"

namespace quarkbot {

///Streaming type - listen on fills from other sources - for example from other strategies, liquidation engine or user's manual trades.
struct ExternalFill : public Fill, public TradableInstrumentStreamTypeItem {
    static constexpr Type type = "external_fill";
    Fill &view() {return *this;}

    ExternalFill(Fill f):Fill(std::move(f)) {};
};


}