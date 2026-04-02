#pragma once
#include "defs.hpp"
#include "ifc/stream_defs.hpp"
#include "types.hpp"
#include "utils/decimal.hpp"
#include "utils/ref_count.hpp"
#include <chrono>

namespace quarkbot {



struct Quote : StreamTypeItem {
    Decimal bid;
    Decimal bid_size;
    Decimal ask;
    Decimal ask_size;
    std::chrono::system_clock::time_point time;
    static constexpr Type type = "quote";
};

struct Trade : StreamTypeItem {
    Decimal price;
    Decimal size;
    std::chrono::system_clock::time_point time;
    static constexpr Type type = "trade";
};

struct OrderBookEntry : StreamTypeItem {
    Decimal price = {}; // price level
    Decimal size = {};  // new size (if <= 0 then remove the level)
    static constexpr Type type = "orderbook_increment";
};


template<typename X>
struct StreamSingleParam: StreamParams {
    X param;
};

struct ClosedBar: StreamTypeItem {
    Decimal open;
    Decimal high;
    Decimal low;
    Decimal close;
    Decimal volume; //volume is optional, if not available, it is set to zero
    static constexpr Type type = "closed_bar";
};

template<unsigned int _interval_sec>
struct ClosedBarInterval: ClosedBar {
    constexpr static auto params = StreamSingleParam<unsigned int>{{},_interval_sec};
};

struct OrderBookLevel {
    OrderBookEntry ask;
    OrderBookEntry bid;
};

struct OrderBookBase {
    std::chrono::system_clock::time_point time;
    OrderBookLevel level[1];     

    OrderBookLevel &operator[](unsigned int l) {
        OrderBookLevel *levels = level;
        return levels[l];
    }
};    

template<unsigned int depth>
class OrderBook: public OrderBookBase,  public StreamTypeItem {
public:
    OrderBookLevel other_levels[1];     
    static constexpr Type type = "orderbook_snapshot";
    constexpr static auto params = StreamSingleParam<unsigned int>{{},depth};
};

struct TradeCounter : public StreamTypeItem {
    static constexpr Type type = "trade_counters";
    /// total count of trades
    std::uint64_t trades = 0;
    /// total volume - it can reset when stream is reopened
    long double volume = static_cast<long double>(0.0);
    /// last price
    Decimal last_price;

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