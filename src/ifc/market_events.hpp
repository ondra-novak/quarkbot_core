#pragma once
#include "ifc/stream_defs.hpp"
#include "types.hpp"
#include "utils/decimal.hpp"
#include <algorithm>
#include <chrono>

namespace quarkbot {



struct Quote : StreamTypeItem {
    Decimal bid;
    Decimal bid_size;
    Decimal ask;
    Decimal ask_size;
    std::chrono::system_clock::time_point time;
    static constexpr Type type = "quote";
    Quote &view() {return *this;}
};

struct Trade : StreamTypeItem {
    Decimal price;
    Decimal size;
    std::chrono::system_clock::time_point time;
    Trade &view() {return *this;}
    static constexpr Type type = "trade";
};

struct OrderBookLevel {
    Decimal price = {}; // price level
    Decimal size = {};  // new size (if <= 0 then remove the level)
};

struct OrderBookIncrement : OrderBookLevel, StreamTypeItem {
    Side side = {};
    std::chrono::system_clock::time_point time;

    OrderBookIncrement &view() {return *this;}
    static constexpr Type type = "orderbook_increment";
};


template<typename X>
struct StreamSingleParam: StreamParams {
    X param;
};

struct ClosedBar  {
    Decimal open = 0;
    Decimal high = 0;
    Decimal low = 0;
    Decimal close = 0;
    Decimal volume = 0; //volume is optional, if not available, it is set to zero
    std::size_t trades = 0; //count of trades. Should be at least 1 otherwise structure is not valid. Value 1 means that no informations about trades available
    std::size_t interval_index = 0;
    ClosedBar &view() {return *this;}
    static constexpr StreamTypeItem::Type type = "closed_bar";
    using ParamType =  StreamSingleParam<unsigned int>;
};

template<unsigned int _interval_sec>
struct ClosedBarInterval: ClosedBar, StreamTypeItem{
    constexpr static auto params =ParamType {{},_interval_sec};
    std::chrono::system_clock::time_point interval_begin() const {
        return std::chrono::system_clock::time_point(std::chrono::seconds(interval_index * _interval_sec));
    }
    std::chrono::system_clock::time_point interval_end() const {
        return std::chrono::system_clock::time_point(std::chrono::seconds(interval_index * (_interval_sec+1)));
    }
};

static_assert(HasStreamParams<ClosedBarInterval<300> >);

struct OrderBookView {
    std::span<OrderBookLevel> bids = {};
    std::span<OrderBookLevel> asks = {};
     std::chrono::system_clock::time_point *time = nullptr;

    OrderBookView() = default;
    OrderBookView(std::span<OrderBookLevel> bids,std::span<OrderBookLevel> asks,std::chrono::system_clock::time_point *time)
        :bids(bids),asks(asks),time(time) {}

    OrderBookView &operator=(const OrderBookView &other) noexcept{
        if (this != &other) {
            auto dbids = std::min(bids.size(), other.bids.size());
            auto dasks = std::min(asks.size(), other.asks.size());
            std::copy_n(other.bids.begin(), dbids, bids.begin());
            std::copy_n(other.asks.begin(), dasks, asks.begin());
            if (time && other.time) *time = *other.time;
        }        
        return *this;
    }
};


template<unsigned int depth>
struct OrderBook: StreamTypeItem {
public:
    std::chrono::system_clock::time_point time;
    std::array<OrderBookLevel,depth> bids; 
    std::array<OrderBookLevel,depth> asks;     

    OrderBookView view() {return OrderBookView(bids,asks,&time);}

    static constexpr Type type = "orderbook_snapshot";
};

struct TradeCounter : public StreamTypeItem {
    static constexpr Type type = "trade_counters";
    /// total count of trades
    std::uint64_t trades = 0;
    /// total volume - it can reset when stream is reopened
    Decimal volume ={};
    /// last price
    Decimal last_price = {};

    std::chrono::system_clock::time_point time;
};

struct ExternalFill : public Fill, public InstrumentStreamTypeItem {
    static constexpr Type type = "external_fill";
};

struct FundingEvent : public InstrumentStreamTypeItem {
    /// amount for this funding
    Decimal amount;
    /// rate,  if the funding is in different currency,
    double rate = 1.0;

    static constexpr Type type = "funding";
};

} // namespace quarkbot