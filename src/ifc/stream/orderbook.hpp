#pragma once

#include "../stream_defs.hpp"
#include "../types.hpp"

namespace quarkbot {
struct OrderBookLevel {
    Decimal price = {}; // price level
    Decimal size = {};  // new size (if <= 0 then remove the level)
};

struct OrderBookIncrement : OrderBookLevel, MarketInstrumentStreamTypeItem {
    Side side = {};
    std::chrono::system_clock::time_point time;

    OrderBookIncrement &view() {return *this;}
    static constexpr Type type = "orderbook_increment";
};


struct OrderBookView {
    std::span<OrderBookLevel> bids = {};
    std::span<OrderBookLevel> asks = {};
    std::chrono::system_clock::time_point time;

    OrderBookView() = default;
    OrderBookView(std::span<OrderBookLevel> bids,std::span<OrderBookLevel> asks,std::chrono::system_clock::time_point time)
        :bids(bids),asks(asks),time(time) {}

    OrderBookView &operator=(const OrderBookView &other) noexcept{
        if (this != &other) {
            auto dbids = std::min(bids.size(), other.bids.size());
            auto dasks = std::min(asks.size(), other.asks.size());
            std::copy_n(other.bids.begin(), dbids, bids.begin());
            std::copy_n(other.asks.begin(), dasks, asks.begin());
            time = other.time;
        }        
        return *this;
    }
};


template<unsigned int depth>
struct OrderBook: MarketInstrumentStreamTypeItem {
public:
    constexpr static auto params =StreamSingleParam<unsigned int> {{},depth};
    std::chrono::system_clock::time_point time;
    std::array<OrderBookLevel,depth> bids; 
    std::array<OrderBookLevel,depth> asks;     

    OrderBookView view() {return OrderBookView(bids,asks,&time);}

    static constexpr Type type = "orderbook_snapshot";
};




}