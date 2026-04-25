#pragma once 
#include "ifc/defs.hpp"
#include "ifc/market_instrument.hpp"
#include "impl/streaming/lock_free_publisher.hpp"
#include "publisher_manager.hpp"
#include <chrono>


namespace quarkbot {


class OrderbookState {
public:

    static constexpr std::size_t increment_queue_len = 1024;
    using IncrementPublisher = LockFreePublisher<OrderBookIncrement, increment_queue_len>;
    using SnapshotPublisher = LockFreePublisher<OrderBookView, 1>;

    static constexpr std::size_t max_levels = 64;

    OrderbookState(PublisherManager &manager, PMarketInstrument instrument)
        :_manager(manager), _instrument(std::move(instrument)) {}

    PublisherManager::PPublisher create_increment_publisher() {
        return std::make_shared<IncrementPublisher>();
    }

    PublisherManager::PPublisher create_snapshot_publisher() {
        return std::make_shared<SnapshotPublisher>();
    }

    bool accept_increment(std::span<OrderBookIncrement> states) {

        bool any = false;

        _manager.enum_all_publishers(_instrument, {}, OrderBookIncrement::type, 
            [&](const StreamParams *,  const PublisherManager::PPublisher &pub){
                IncrementPublisher &p = static_cast<IncrementPublisher &>(*pub);
                for (auto &x: states) {
                    p.write([&](OrderBookIncrement &t) noexcept{
                        t = x;return true;
                    });
                }
                any = true;
            });

        _newasks.clear();
        _newbids.clear();
        auto newbank = 1-_bank;
        for (const auto &x:states) {
            if (x.side == Side::buy) {
                _newbids.push_back({x.price, x.size});
            } else if (x.side == Side::sell) {
                _newasks.push_back({x.price, x.size});
            }
            _tp[newbank] = std::max(_tp[newbank], x.time);                
        }
        std::sort(_newasks.begin(), _newasks.end(), sort_asks);
        std::sort(_newbids.begin(), _newbids.end(), sort_bids);

        merge_orderbook<Side::buy>(_bids[_bank],_bids[newbank], _newbids);
        merge_orderbook<Side::sell>(_asks[_bank],_asks[newbank], _newasks);
        _bank = newbank;



        _manager.enum_all_publishers(_instrument, {}, OrderBook<1>::type, 
            [&](const StreamParams *, const PublisherManager::PPublisher &pub){
                SnapshotPublisher &p = static_cast<SnapshotPublisher &>(*pub);
                OrderBookView view(_bids[_bank],_asks[_bank], &_tp[_bank]);
                p.write([&](OrderBookView &v)noexcept{
                    v = view;
                    return true;
                });
                any = true;
            });
        return any;

    }

protected:

    PublisherManager &_manager;
    PMarketInstrument _instrument;


    std::array<std::array<OrderBookLevel, max_levels>, 2> _bids = {};
    std::array<std::array<OrderBookLevel, max_levels>, 2> _asks = {};
    std::vector<OrderBookLevel> _newbids = {};
    std::vector<OrderBookLevel> _newasks = {};
    std::size_t _bank = 0;
    std::chrono::system_clock::time_point _tp[2];



    static bool sort_asks(const OrderBookLevel &a, const OrderBookLevel &b) {
        if (a.price == 0) return a.price != 0;
        if (b.price == 0) return false;
        return a.price < b.price;
    }
    static bool sort_bids(const OrderBookLevel &a, const OrderBookLevel &b) {
        return a.price > b.price;
    }

    template<Side side>
    void merge_orderbook(std::span<OrderBookLevel> old, std::span<OrderBookLevel> updated, std::span<OrderBookLevel> inc) {
        constexpr auto cmp = side == Side::buy ? sort_bids:sort_asks;
        auto old_i = old.begin();
        auto old_e = old.end();
        auto up_i = updated.begin();
        auto up_e = updated.end();
        auto inc_i =inc.begin();
        auto inc_e = inc.end();
        while (old_i != old_e && inc_i != inc_e && up_i != up_e) {
            if (cmp(*old_i, *inc_i)) {
                *up_i++ = *old_i++;
            } else if (cmp(*inc_i, *old_i)) {
                if (inc_i->size) {
                    *up_i++ = *inc_i;
                }
                ++inc_i;
            } else {
                if (inc_i->size) {
                    *up_i++ = *inc_i;
                }
                ++inc_i;
                ++up_i;
            }
        }
        while (old_i != old_e &&  up_i != up_e) { 
            *up_i++ = *old_i++;
        }
        while (inc_i != inc_e && up_i != up_e) {
            *up_i++ = *inc_i++;
        }
        while (up_i != up_e) {
            up_i->price = 0;
            up_i->size = 0;
        }
    }



};


}