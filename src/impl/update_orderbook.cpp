
#include "update_orderbook.hpp"
#include "ifc/market_events.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <memory_resource>
#include <system_error>
#include <type_traits>
namespace quarkbot {

static std::pmr::synchronized_pool_resource pool;

OrderBook alloc_orderbook() {
    auto ptr = pool.allocate(sizeof(OrderBookSnapshot));
    auto ref = static_cast<OrderBookSnapshot *>(ptr);
    std::construct_at(ref);
    return OrderBook(ref, OrderBookSnapshotDeleter{
        [](const OrderBookSnapshot *x){
            std::destroy_at(x);
            pool.deallocate(const_cast<OrderBookSnapshot *>(x), sizeof(OrderBookSnapshot));
        }});
    

}

template<typename Cmp>
requires (std::is_invocable_r_v<bool, Cmp, const OrderBookEntry &, const OrderBookEntry &> )
static void merge_orderbook(std::span<const OrderBookEntry> cur,
            std::span<const OrderBookEntry> increment, 
            std::array<OrderBookEntry, OrderBookSnapshot::max_depth> &result,
            Cmp cmp
) {
    auto cur_iter = cur.begin();
    auto cur_end = cur.end();
    auto inc_iter = increment.begin();
    auto inc_end = increment.end();
    auto wr = result.begin();
    auto wr_end = result.end();
    
    while (wr != wr_end && cur_iter != cur_end && inc_iter != inc_end) {
        if (cmp(*cur_iter, *inc_iter)) {
            *wr++ = *cur_iter++;
        } else if (cmp(*inc_iter,*cur_iter)) {
            *wr++ = *inc_iter++;
        } else {
            *wr++ = *inc_iter++;
            ++cur_iter;
        }
    }
    while (wr != wr_end && cur_iter != cur_end) {
        *wr++ = *cur_iter++;
    }
    while (wr != wr_end && inc_iter != inc_end) {
        *wr++ = *inc_iter++;
    }

}

OrderBook update_orderbook(const OrderBook &other, std::span<OrderBookEntry> bids, std::span<OrderBookEntry> asks, std::chrono::system_clock::time_point tp) {
    OrderBook new_rev;
    if (!other) {
        return update_orderbook(alloc_orderbook(), bids,asks,tp);    
    }
    new_rev = alloc_orderbook();
    OrderBookSnapshot *ptr = const_cast<OrderBookSnapshot *>(new_rev.get());

    std::sort(bids.begin(), bids.end(), OrderBook::bids_sort);
    std::sort(asks.begin(), asks.end(), OrderBook::asks_sort);

    merge_orderbook(other->asks, asks, ptr->asks, OrderBook::asks_sort);
    merge_orderbook(other->bids, bids, ptr->bids, OrderBook::bids_sort);
    ptr->time = tp;
    return new_rev;






}


}