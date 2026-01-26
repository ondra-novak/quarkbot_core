#pragma once

#include "ifc/market_events.hpp"
#include <chrono>
namespace quarkbot {


OrderBook update_orderbook(const OrderBook &other, std::span<OrderBookEntry> bids, std::span<OrderBookEntry> asks, std::chrono::system_clock::time_point tp);


}