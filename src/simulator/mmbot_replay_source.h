#pragma once

#include "replay.h"


namespace quarkbot {

namespace Replay {

struct MMBotSourceConfig {
    ///associated symbol string
    std::string symbol;
    ///random generator seed
    std::size_t seed = 0;
    ///count of generated market events (non-trade) between time points
    /** trade is manifested only on time point.
     * Set zero to disable generate non-trade market events  */
    unsigned int market_events_per_minute = 10;
    ///max spread in percent
    double spread_percent = 0.1;
    ///minimal volume for thread and ask-bid
    double min_volume = 1;
    ///maximal volume for thread and ask-bid
    double max_volume = 100;
    ///speed multiplier
    /** by default, each time point is 1 minute, but you can change speed */
    double speed = 1.0;
    /** offset when this replay starts */
    double offset = 0.0;

};


Source create(const std::string &fname, std::chrono::system_clock::time_point initial_time, MMBotSourceConfig config);
Source create(std::istream &src, std::chrono::system_clock::time_point initial_time,  MMBotSourceConfig config);




}

} /* namespace trading_api */


