#pragma once

#include "quarkbot/abstract/backtest_data_source.hpp"
#include "trth_raw_source.hpp"
#include <quarkbot/stream/auction.hpp>
#include <quarkbot/stream/quote.hpp>
#include <quarkbot/stream/trade.hpp>
#include <filesystem>


namespace quarkbot {

class TRTHEventSource {
public:

    ///open TRTH (LSEG) data source
    TRTHEventSource(std::filesystem::path file);

    ///Retrieve next event
    /**
        @param ev reference to variable filled with event
        @retval true success
        @retval false - eof reached
    */
    bool operator()(BacktestEvent &ev);



protected:

    TRTHRawSource _raw_source;
    bool _eof = false;
    TRTHRawSource::Data _data;
    ///row read ahead and not consumed yet
    TRTHRawSource::Data _next;
    bool _has_next = false;

    ///read next row, the row read ahead first
    bool read_row(TRTHRawSource::Data &data);
    ///read the row following _data into _next, false at eof
    bool peek_row();
    ///fill the Auction from _data and the rows following it
    bool parse_auction(Auction &a);
    ///fill the Trade from _data and the rows following it
    bool parse_trade(Trade &t);


};

}