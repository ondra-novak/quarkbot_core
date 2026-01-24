#pragma once
#include "defs.hpp"
#include <chrono>

namespace quarkbot {

struct Quote : StreamTypeItem {
  double bid;
  double bid_size;
  double ask;
  double ask_size;
  std::chrono::system_clock::time_point time;
};

struct Trade : StreamTypeItem {
  double price;
  double size;
  std::chrono::system_clock::time_point time;
};

struct OrderBookEntry {
  double price;
  double size;
};

struct OrderBook : StreamTypeItem {
  std::vector<OrderBookEntry> bids;
  std::vector<OrderBookEntry> asks;
  std::chrono::system_clock::time_point time;
};

} // namespace quarkbot