#pragma once

#include "ifc/stream/closedbar.hpp"
#include "ifc/stream/quote.hpp"
namespace quarkbot {

    inline auto calculate_closed_bar(const Trade &tr) {
        return  [&](ClosedBar::Param interval, auto &publisher){
            std::optional<ClosedBar> newval;
            publisher.write([&](ClosedBar &x) noexcept {
                //calculate new value
                auto nw = x.add(tr, interval);
                //test if we creating new candle
                if (nw.interval_index != x.interval_index) {
                    //if it is new candle remember new value outside
                    newval = nw;
                    //publish previous candle
                    return x.interval_index != 0;   //don't publish zero interval candles
                }
                //no new candle, update candle
                x = nw;
                //don't publish yet
                return false;
            });
            //if new candle
            if (newval.has_value()) {
                publisher.write([&](ClosedBar &x) noexcept {
                    //initialize staging storage with new candle
                    x = *newval;
                    //don't publish yet
                    return false;
                });
            }
        };

    }
    //it just check for time and publish event if time outside of time window
    inline auto calculate_closed_bar(const Quote &qt) {
        return  [&](ClosedBar::Param interval, auto &publisher){
            bool published;
            publisher.write([&](ClosedBar &x) noexcept {
                published = x.interval_index != x.to_interval_index(qt.time, interval);
                return published;
            });
            if (published) {
                publisher.write([&](ClosedBar &x) noexcept {
                    x = ClosedBar{}; //initialize
                    return false;   
                });
            }
        };
    }

}