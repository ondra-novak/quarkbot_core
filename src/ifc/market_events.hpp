#pragma once
#include "defs.hpp"
#include "types.hpp"
#include "utils/fixed.hpp"
#include "utils/ref_count.hpp"
#include <chrono>

namespace quarkbot {


struct Quote : MarketStreamTypeItem {
  Fixed bid;
  Fixed bid_size;
  Fixed ask;
  Fixed ask_size;
  std::chrono::system_clock::time_point time;
  static constexpr Type type = "quote";
};

struct Trade : MarketStreamTypeItem {
  Fixed price;
  Fixed size;
  std::chrono::system_clock::time_point time;
  static constexpr Type type = "trade";
};

struct OrderBookEntry : MarketStreamTypeItem {
  Fixed price = {}; //price level
  Fixed size = {};  //new size (if <= 0 then remove the level)
  static constexpr Type type = "orderbook_increment";
};

struct OrderBookSnapshot : MarketStreamTypeItem {
  static constexpr std::size_t max_depth = 50;
  mutable std::atomic<int> _ref_count = {};
  std::array<OrderBookEntry, max_depth> bids;
  std::array<OrderBookEntry, max_depth> asks;
  std::chrono::system_clock::time_point time;  
};

struct OrderBookSnapshotDeleter {
  void (*deleter)(const OrderBookSnapshot *ptr);
  void operator()(const OrderBookSnapshot *ptr) {deleter(ptr);}
};

class OrderBook: public refcnt_ptr<const OrderBookSnapshot, OrderBookSnapshotDeleter>, public MarketStreamTypeItem {
public:
  using refcnt_ptr<const OrderBookSnapshot, OrderBookSnapshotDeleter>::refcnt_ptr;
  static constexpr Type type = "orderbook_snapshot";

  static bool bids_sort(const OrderBookEntry &a, const OrderBookEntry &b) {
      return a.price > b.price;
  }
  static bool asks_sort(const OrderBookEntry &a, const OrderBookEntry &b) {
      if (a.price == Fixed{}) return false;
      if (b.price == Fixed{}) return true;      
      return a.price < b.price;
  }
};


struct TradeCounter: public MarketStreamTypeItem {
    static constexpr Type type = "trade_counters";
    ///total count of trades
    std::uint64_t trades = 0;
    ///total volume - it can reset when stream is reopened
    long double volume = 0.0;
    ///last price
    Fixed last_price;
    
    std::chrono::system_clock::time_point time;
};

struct ExternalFill: public Fill, public InstrumentStreamTypeItem {
    static constexpr Type type = "external_fill";
};

struct FundingEvent: public InstrumentStreamTypeItem {
    ///amount for this funding
    Fixed amount;
    ///rate,  if the funding is in different currency,
    double rate = 1.0;

    static constexpr Type type = "funding";
};


} // namespace quarkbot