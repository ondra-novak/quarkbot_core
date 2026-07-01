#pragma once

#include "quarkbot/stream/rangedbar.hpp"

namespace quarkbot {
    inline auto calculate_ranged_bar(const Trade &tr) {
        return [&](RangedBar::Param range, auto &publisher){
            std::optional<RangedBar> newval;
            //write to staging state
            publisher.write([&](RangedBar &x) noexcept {
                //calculate new value
                auto nw = x.add(tr, range);
                //write new value to staging storage
                x = nw.first;
                //if it break from range?
                if (nw.second) {
                    //has current candle an open
                    if (x.open > 0) {
                        //generate open candle
                        newval = x.init_open();
                        //publish current staging storage
                        return true;
                    } else {
                        //generate brand new candle starting at current price
                        newval = RangedBar{{},
                            tr.price, tr.price, tr.price, tr.price, 0,
                             false,tr.time, tr.time};
                        //don't publish anything yet
                        return false;
                    }
                }
                //no break, keep staging unpublished
                return false;
                
            });
            //if new candle generated
            if (newval.has_value()) {
                //initialize new staging storage
                publisher.write([&](RangedBar &x) noexcept {
                    //write initial candle
                    x = newval->add(tr,range).first;
                    //don't publish yet
                    return false;
                });
            }
        };

    }
}
