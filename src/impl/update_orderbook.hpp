#pragma once

namespace quarkbot {


    #if 0
    template<unsigned int depth>
    struct OrderBookTools {

        static OrderBook<depth> alloc_orderbook() {
            auto ptr = std::allocate_shared<OrderBookSnapshot<depth> >(std::pmr::polymorphic_allocator<OrderBookSnapshot<depth>>{&mem_pool});
            return OrderBook<depth>(ptr);            
        }

        template<typename Cmp>
        requires (std::is_invocable_r_v<bool, Cmp, const OrderBookEntry &, const OrderBookEntry &> )
        static void merge_orderbook(std::span<const OrderBookEntry> cur,
                    std::span<const OrderBookEntry> increment, 
                    std::array<OrderBookEntry, depth> &result,
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

        OrderBook<depth> update_orderbook(const OrderBook<depth> &other, std::span<OrderBookEntry> bids, std::span<OrderBookEntry> asks, std::chrono::system_clock::time_point tp) {
            OrderBook<depth> new_rev;
            if (!other) {
                return update_orderbook(OrderBookTools<depth>::alloc_orderbook(), bids,asks,tp);    
            }
            new_rev = OrderBookTools<depth>::alloc_orderbook();
            auto ptr = std::const_pointer_cast<OrderBookSnapshot<depth> >(static_cast<std::shared_ptr<const OrderBookSnapshot<depth> > >(new_rev));
            

            std::sort(bids.begin(), bids.end(), OrderBook<depth>::bids_sort);
            std::sort(asks.begin(), asks.end(), OrderBook<depth>::asks_sort);

            OrderBookTools<depth>::merge_orderbook(other->asks, asks, ptr->asks, OrderBook<depth>::asks_sort);
            OrderBookTools<depth>::merge_orderbook(other->bids, bids, ptr->bids, OrderBook<depth>::bids_sort);
            ptr->time = tp;
            return new_rev;

        }
    };

    template<unsigned int max_depth, typename Callback>
    inline void update_orderbook(const StreamTypeItem *current_snapshot, unsigned int depth,
                    std::span<OrderBookEntry> bids, std::span<OrderBookEntry> asks, std::chrono::system_clock::time_point tp,
                    Callback &&cb) {
        if constexpr(max_depth == 0) {
            return;
        }
        if (max_depth > depth) {
            update_orderbook<max_depth, Callback>(current_snapshot, depth, bids, asks, tp, std::forward<Callback>(cb));
        } else {
            if (current_snapshot )
            OrderBookTools<max_depth>::update_orderbook
        }


    }



    template<unsigned int depth, typename Fn, unsigned int level = depth>
    void shrink_orderbook(const OrderBook<depth> &source, unsigned int target_size, Fn &&cb) {
        if constexpr(level > 1) {
            if (level >= target_size) {
                shrink_orderbook<depth, Fn, level-1>(source, target_size, std::forward<Fn>(cb));
            }
        } 
        auto out = OrderBookTools<level>::alloc_orderbook();                
        
    }

    #endif

}