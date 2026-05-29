#pragma once

#include "../stream_defs.hpp"
#include "../types.hpp"
#include "ifc/stream/trade.hpp"


namespace quarkbot {

struct DecimalRange {
    uint64_t encoded;
    constexpr DecimalRange(Decimal val):encoded(std::bit_cast<uint64_t>(val)) {}
    constexpr Decimal as_decimal() const {return std::bit_cast<Decimal>(encoded);  }
};

struct RangedBar: MarketInstrumentStreamTypeItem {
    static constexpr MarketInstrumentStreamTypeItem::Type type = "ranged_bar";
    using ParamType =  StreamSingleParam<Decimal>;
    Decimal open = 0;
    Decimal high = 0;
    Decimal low = 0;
    Decimal close = 0;
    Decimal volume = 0; //volume is optional, if not available, it is set to zero

    /**
    gap indication. This indicates, that current tick was too far from the previous, 
    which would otherwise create virtual candles. As the virtual candles creation 
    isn't  supported. reader should create them by own if they need them.

    When gap is true, (close - high) or (low - close) is larger than specified range.
    The actual value of close is (last - range) or (last + range) (depends on direction)
    The new candle is opened 
     */
    bool gap = false; 
    std::chrono::system_clock::time_point open_tp = {};
    std::chrono::system_clock::time_point close_tp = {};

    RangedBar &view() {return *this;}

    ///initialize new candle
    RangedBar init_open() {
        return {*this, close, close, close, close,
             0, false, close_tp, close_tp};
    }

    ///add trade
    /**
    @param tr trade
    @param range range used for this candle
    @return updated candle. The second value contains true, if range was broken.
    In this case returned candle is final current candle, and new candle must be 
    open by init_open() on returned candle
     */
    std::pair<RangedBar, bool> add(const Trade &tr, Decimal range) const {
        auto new_close = tr.price;
        auto from_low = new_close - low;
        auto from_high = high - new_close;

        bool gap = false;
        bool broken = false;
        if (from_low > range) {
            broken = true;
            if (from_low > 2*range) {
                gap = true;
                new_close = tr.price - range;
            } else {
                new_close = low + range;
            }
        } else if (from_high > range) {
            broken = true;
            if (from_high > 2*range) {
                gap = true;
                new_close = high - range;
            } else {
                new_close = high - range;
            }        
        }
        return {
            {*this, open, std::max(high, new_close), std::min(low, new_close),
            new_close, broken?volume:volume + tr.size, gap, open_tp, tr.time}
            ,broken
        };
    }

    using Param = Decimal;
};



}