#pragma once

#include "quarkbot/stream/closedbar.hpp"
#include "quarkbot/stream/quote.hpp"
#include <chrono>
namespace quarkbot {

    inline auto calculate_closed_bar(const Trade &tr) {
        return  [&](ClosedBar::Param interval, auto &publisher){
            std::optional<ClosedBar> newval;
            publisher.write([&](ClosedBar &x) noexcept {
                //calculate new value
                auto nw = x.add(tr, interval);
                //test if we creating new candle
                if (nw.start_time != x.start_time) {
                    //if it is new candle remember new value outside
                    newval = nw;
                    //publish previous candle
                    return x.start_time != std::chrono::system_clock::time_point{};   //don't publish zero interval candles
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
                published = x.start_time != x.interval_lower_bound(qt.time, interval);
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