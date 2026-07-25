#pragma once

#include "../types.hpp"
#include "quarkbot/utils/refcnt.hpp"
#include <atomic>
#include <chrono>


namespace quarkbot {
struct OrderBookLevel {
    Decimal price = {}; // price level
    Decimal quantity = {};  // new size (if <= 0 then remove the level)    

    //bids, high price goes first
    static bool order_bids(const OrderBookLevel &a, const OrderBookLevel &b) {
        return b.price < a.price;
    }
    //asks, low price  goes first
    static bool order_asks(const OrderBookLevel &a, const OrderBookLevel &b) {
        return a.price < b.price;
    }
    //this is erase increment
    static bool is_erase(const OrderBookLevel &a) {
        return !a.quantity;
    }

};

constexpr auto orderbook_level_max = OrderBookLevel{Decimal::max(), 0};
constexpr auto orderbook_level_min = OrderBookLevel{0, 0};

struct OrderBookIncrement : OrderBookLevel {
    struct MarketInstrumentStream {};    
    Side side = {};
    std::chrono::system_clock::time_point time;
};


struct OrderBook {
    struct MarketInstrumentStream {};    
    ///bids
    std::span<const OrderBookLevel> bids;
    ///asks
    std::span<const OrderBookLevel> asks;
    ///time of snapshot
    std::chrono::system_clock::time_point time;
    ///holds reference to snapshot to keep lifetime
    RefCountPtr<RefCountInstanceWithDeleter> snapshot_ptr;

};



}