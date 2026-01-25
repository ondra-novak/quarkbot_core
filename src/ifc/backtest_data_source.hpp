#pragma once

#include "market_events.hpp"
#include <chrono>
#include <optional>
namespace quarkbot {

class IBacktestDataSource {
    
    enum EventType {
        quote,
        trade,
        orderbook
    };

    struct Event {
        EventType type;
        std::string instrument;
        std::chrono::system_clock::time_point time;
    };


    virtual std::optional<Event>  next_event() = 0;
    virtual Quote get_quite() const = 0;
    virtual Trade get_trade() const = 0;
    virtual OrderBookEntry get_orderbook_increment() const = 0;

};

}