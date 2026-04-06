#pragma once

#include "market_events.hpp"
#include <chrono>
#include <variant>

namespace quarkbot {


    class IBacktestDataSource {
    public:        

        using EventData =  std::variant<Quote, Trade, OrderBookIncrement>;
        struct Event {
            std::chrono::system_clock::time_point time;
            std::string instrument;
            EventData payload;
        };

        virtual std::optional<Event> next_event()  = 0;
        virtual ~IBacktestDataSource() = default;
    };




    





}