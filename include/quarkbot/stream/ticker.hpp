#pragma once
#include "tradestat.hpp"
#include "quote.hpp"


namespace quarkbot {

//contains stats and last quote

struct Ticker  {


    TradeStatCounter stats;
    Quote quote;

    Ticker add(const Trade &tr) const {
        return {stats.add(tr), quote};
    }
    Ticker add(const Quote &qt) const {
        return {stats, qt};
    }

};


}