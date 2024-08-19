#pragma once

#include "timer.h"
#include "decimal.h"

#include <chrono>
#include <string>


namespace trading_api {

class TickData {
public:
    //note - if bid and ask are not supported, then bid = ask = last

    Timestamp tp = {};  //snapshot time
    Decimal bid = 0;  //current bid price
    Decimal bid_volume = 0; //current volume on bid
    Decimal ask = 0;  //current ask price
    Decimal ask_volume = 0; //current volume on ask
    Decimal last = 0;    //last execution price - if zero, no trade has been reported or not supported
    Decimal volume = 0;  //execution volume from last update - if unsupported = 0
    Decimal index = 0;   //index price (if known) otherwise zero

    friend std::ostream &operator << (std::ostream &s, const TickData &tk) {
        s << tk.bid << '(' << tk.bid_volume << ") <-- " << tk.last << '(' << tk.volume << ')' << " --> " << tk.ask << '(' << tk.ask_volume << ')';
        return s;
    }

};


}
