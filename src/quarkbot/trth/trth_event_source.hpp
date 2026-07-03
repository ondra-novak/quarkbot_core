#pragma once

#include "quarkbot/abstract/backtest_data_source.hpp"
#include "trth_raw_source.hpp"
#include <quarkbot/stream/auction.hpp>
#include <quarkbot/stream/quote.hpp>
#include <quarkbot/stream/trade.hpp>
#include <chrono>
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


};

}