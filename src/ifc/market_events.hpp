#pragma once
#include "defs.hpp"
#include "utils/fixed.hpp"
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
  Fixed price; //price level
  Fixed size;  //new size (if <= 0 then remove the level)
};

struct OrderBook : StreamTypeItem {
  static constexpr Type type = "orderbook_snapshot";
  std::vector<OrderBookEntry> bids;
  std::vector<OrderBookEntry> asks;
  std::chrono::system_clock::time_point time;
};

} // namespace quarkbot