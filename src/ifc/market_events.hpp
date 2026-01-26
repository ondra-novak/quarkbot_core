#pragma once
#include "defs.hpp"
#include "utils/fixed.hpp"
#include "utils/ref_count.hpp"
#include <chrono>

namespace quarkbot {


struct Quote : StreamTypeItem {
  static constexpr Type type = "quote";
  Fixed bid;
  Fixed bid_size;
  Fixed ask;
  Fixed ask_size;
  std::chrono::system_clock::time_point time;
};

struct Trade : StreamTypeItem {
  static constexpr Type type = "trade";
  Fixed price;
  Fixed size;
  std::chrono::system_clock::time_point time;
};

struct OrderBookEntry : StreamTypeItem {
  static constexpr Type type = "orderbook_increment";
  Fixed price = {}; //price level
  Fixed size = {};  //new size (if <= 0 then remove the level)
};

struct OrderBookSnapshot : StreamTypeItem {
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

class OrderBook: public refcnt_ptr<const OrderBookSnapshot, OrderBookSnapshotDeleter>, public StreamTypeItem {
public:
  using refcnt_ptr<const OrderBookSnapshot, OrderBookSnapshotDeleter>::refcnt_ptr;
  static constexpr Type type = "orderbook_snapshot";

  static bool bids_sort(const OrderBookEntry &a, const OrderBookEntry &b) {
      return a.price > b.price;
  }
  static bool asks_sort(const OrderBookEntry &a, const OrderBookEntry &b) {
      if (a.price <= 0) return false;
      if (b.price <= 0) return true;      
      return a.price < b.price;
  }
};

} // namespace quarkbot