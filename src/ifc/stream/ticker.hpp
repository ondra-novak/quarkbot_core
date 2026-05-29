#pragma once
#include "tradestat.hpp"
#include "quote.hpp"


namespace quarkbot {

//contains stats and last quote

struct Ticker: TradeStatCounter, Quote {

    Ticker add(const Trade &tr) const {
        return {TradeCounter::add(tr), *this};
    }
    Ticker add(const Quote &qt) const {
        return {*this, qt};
    }

};


}