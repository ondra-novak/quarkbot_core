#pragma once

#include "../types.hpp"
#include <compare>


namespace quarkbot {
struct OrderBookLevel {
    Decimal price = {}; // price level
    Decimal quantity = {};  // new size (if <= 0 then remove the level)    

    static int order_bids(const OrderBookLevel &a, const OrderBookLevel &b) {
        return sgn(b.price - a.price);
    }
    static int order_asks(const OrderBookLevel &a, const OrderBookLevel &b) {
        return sgn(a.price - b.price);
    }
    static bool is_zero_level(const OrderBookLevel &a) {
        return !!a.quantity;
    }

};

constexpr auto orderbook_level_max = OrderBookLevel{Decimal::max(), 0};
constexpr auto orderbook_level_min = OrderBookLevel{0, 0};

struct OrderBookIncrement : OrderBookLevel {
    struct PMarketInstrumentStream {};    
    Side side = {};
    std::chrono::system_clock::time_point time;

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
            while (dasks < asks.size()) {
                asks[dasks++] = orderbook_level_max;
            }
        }        
        return *this;
    }
};


template<typename Out, typename In1, typename In2, typename Cmp, typename Zero>
inline Out orderbook_merge_level(In1 beg1, In1 end1, In2 beg2, In2 end2, Out begO, Out endO, Cmp cmp, Zero zero) {
    while (beg1 != end1 && beg2 != end2 && begO != endO) {
        auto r = cmp(*beg1, *beg2);
        if (r<0) {*begO = *beg1;++begO;++beg1;}
        else if (r>0) { if (!zero(*beg2)) {*begO = *beg2;++begO;} ++beg2;}
        else {  if (!zero(*beg2)) {*begO = *beg2;++begO;} ++beg1;++beg2;};
    }
    while (beg1 != end1 && begO != endO) {
        *begO = *beg1;++begO;++beg1;
    }
    while (beg2 != end2 && begO != endO) {
        if (!zero(*beg2)) {*begO = *beg2;++begO;} 
        ++beg2;
    }
    return begO;

}


template<unsigned int depth>
struct OrderBook {
public:
    struct MarketInstrumentStream {};

    std::chrono::system_clock::time_point time;
    std::array<OrderBookLevel,depth> bids = {orderbook_level_min};
    std::array<OrderBookLevel,depth> asks = {orderbook_level_max};

    OrderBookView view() {return OrderBookView(bids,asks,&time);}

    using Param = unsigned int;
    static constexpr Param param = depth;




    ///Creates new orderbook state from current state and increment
    /**
    @param target target state
    @param increment contains increment, just changed values, ordered. Level with quantity == 0 is removed
     */
    void apply_to(OrderBook<depth> &target, const OrderBookView &increment) const {
        orderbook_merge_level(bids.begin(), bids.end(), 
                    increment.bids.begin(),increment.bids.end(),
                    target.bids.begin(), target.bids.end(),
                    OrderBookLevel::order_bids, OrderBookLevel::is_zero_level);
        auto p = orderbook_merge_level(asks.begin(), asks.end(), 
                    increment.asks.begin(),increment.asks.end(),
                    target.asks.begin(), target.asks.end(),
                    OrderBookLevel::order_asks, OrderBookLevel::is_zero_level);
        while (p != target.asks.end()) {
            *p = orderbook_level_max;            
        }
        target.time = increment.time;
    }

};




}