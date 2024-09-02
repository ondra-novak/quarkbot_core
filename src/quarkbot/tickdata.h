#pragma once


namespace quarkbot {


class TickData {
public:

    Timestamp tp = {};  //snapshot time

    Decimal bid = 0;  //current bid price
    Decimal ask = 0;  //current ask price
    Decimal last = 0;    //last execution price
    Decimal index = 0;   //index price (if known) otherwise zero
    double bid_volume = 0; //current volume on bid
    double ask_volume = 0; //current volume on ask
    double cum_volume = 0;  //cumulative volume
    unsigned long cum_trades = 0; //total trades

    friend std::ostream &operator << (std::ostream &s, const TickData &tk) {
        s << tk.bid << '(' << tk.bid_volume << ") <-- " << tk.last << '(' << tk.cum_volume << ')' << " --> " << tk.ask << '(' << tk.ask_volume << ')';
        return s;
    }

    ///retrieve count trades happened between two market events
    friend unsigned long count_trades(const TickData &prev, const TickData &cur) {
        return cur.cum_trades > prev.cum_trades?cur.cum_trades - prev.cum_trades:cur.cum_trades;
    }

    ///retrieve total volume happened betwenn two market events
    friend double volume(const TickData &prev, const TickData &cur) {
        return cur.cum_volume > prev.cum_volume?cur.cum_volume - prev.cum_volume:cur.cum_volume;
    }

};


}
