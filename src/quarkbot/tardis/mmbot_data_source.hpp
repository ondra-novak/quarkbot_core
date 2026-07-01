#pragma once

#include "quarkbot/backtest_data_source.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <istream>
#include <optional>

namespace quarkbot {

    class MMBOT_backtest_datasource : public IBacktestDataSource {
    public:

        virtual std::optional<Event> next_event();
        MMBOT_backtest_datasource(std::string instrument, std::filesystem::path path, std::chrono::system_clock::time_point start_time);
        MMBOT_backtest_datasource(std::string instrument, std::istream &stream, std::chrono::system_clock::time_point start_time);


    protected:
        Decimal _prev_price;
        std::ifstream _ifstream;
        std::istream &_stream;
        std::string _instrument;
        std::chrono::system_clock::time_point _tp;
        std::optional<Decimal> _price;
        
    
        std::optional<Decimal> load_number();
    };


}