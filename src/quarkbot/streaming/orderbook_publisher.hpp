#pragma once

#include "publisher_base.hpp"
#include "quarkbot/stream/orderbook.hpp"
#include "quarkbot/utils/refcnt.hpp"
#include "subscriber.hpp"
#include <chrono>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
namespace quarkbot {


class OrderbookPublisher : public PublisherBase, public std::enable_shared_from_this<OrderbookPublisher>{
public:

    static constexpr auto merge_batch_size = 1000UL;
    static constexpr auto max_pool_size = 16UL;

    

    OrderbookPublisher():PublisherBase([](std::shared_ptr<PublisherBase> base) -> std::shared_ptr<IEventStreamBase> {
        auto me = std::static_pointer_cast<OrderbookPublisher>(base);
        return me->subscribe();
    }) {}

    void publish(std::span<const OrderBookLevel> bids, std::span<const OrderBookLevel> asks, std::chrono::system_clock::time_point tp, bool snapshot) {
        std::scoped_lock _(_mx);
        if (snapshot) {
            create_snapshot(bids, asks, tp);
            _bids.clear();
            _asks.clear();

        } else {
            if (bids.empty() && asks.empty()) 
            _bids.insert(_bids.end(), bids.begin(), bids.end());
            _asks.insert(_asks.end(), asks.begin(), asks.end());
            if (_bids.size() > merge_batch_size || _asks.size() > merge_batch_size)  {
                merge_locked(tp);
            } else {
                _last_update = tp;
            }
            
        }
    
        ++_cur_seq;
        PublisherBase::flush_consumers_lk(true);
    }
    
    bool read(OrderBook &ref, Seq &seq) {
        PSnapshot snap;
        {
            std::scoped_lock _(_mx);
            if (!_bids.empty() || !_asks.empty()) {
                merge_locked(_last_update);
            }
            if (_cur_seq == seq) return false;
            seq = _cur_seq;
            snap = _cur_value;
        }
        ref.snapshot_ptr = snap;
        ref.bids = snap->bids;
        ref.asks = snap->asks;
        ref.time = snap->time;
        return true;
    }

     coro::prepared_coro next(Seq seq, awaitable<bool>::result promise, coro::cancel_signal *sig) {
        std::scoped_lock _(_mx);
        if (seq != _cur_seq) return promise(true);
        if (_closed) return promise(false);
        _awaiters.push_back({std::move(promise), sig});
        return {};
     }

     

protected:

    class Snapshot : public RefCountInstanceWithDeleter{
    public:

        Snapshot():RefCountInstanceWithDeleter(snapshot_deleter) {}

        std::weak_ptr<OrderbookPublisher> owner;
        std::vector<OrderBookLevel> bids;
        std::vector<OrderBookLevel> asks;
        std::chrono::system_clock::time_point time;

        static void snapshot_deleter(RefCountInstanceWithDeleter *me) {
            Snapshot *snp = static_cast<Snapshot *>(me);
            auto lk = snp->owner.lock();
            if (lk) lk->release_snapshot(snp);
            else delete snp;
        }

    };

    

    using PSnapshot = RefCountPtr<Snapshot>;
    using SnapshotPool = std::queue<PSnapshot>;


    SnapshotPool _pool;
    std::chrono::system_clock::time_point _last_update;
    PSnapshot _cur_value = {};
    Seq _cur_seq = 0;
    
    

    /// bids to merge
    std::vector<OrderBookLevel> _bids;
    /// asks to merge
    std::vector<OrderBookLevel> _asks;


    std::shared_ptr<IEventStreamBase> subscribe() {
        return std::make_shared<StreamSubscriber<OrderBook, OrderbookPublisher> >(shared_from_this());
    }

    template<typename Cmp>
    void apply_increment(std::span<const OrderBookLevel> snap, std::span<const OrderBookLevel> increment, std::vector<OrderBookLevel> &out, Cmp compare) {
        auto isnp = snap.begin();
        auto iinc = increment.begin();
        auto esnp = snap.end();
        auto einc = increment.end();
        out.clear();        
        while (isnp != esnp && iinc != einc) {
            if (compare(*isnp, *iinc)) {
                out.push_back(*isnp);
                ++isnp;
            } else {
                if (iinc->quantity) {
                    out.push_back(*iinc);
                }
                if (iinc->price == isnp->price) {
                    ++isnp;
                }
                ++iinc;                
            }
        }
        while (isnp != esnp) {
            out.push_back(*isnp);
            ++isnp;
        }
        while (iinc != einc) {
            out.push_back(*iinc);
            ++iinc;
        }
    }

    void remove_dups(std::vector<OrderBookLevel> &list)  {
        auto cnt = list.size();
        if (cnt <2 ) return;
        std::size_t ref = 1;
        std::size_t cur = 0;
        while (ref < cnt) {
            if (list[ref].price != list[cur].price) ++cur;
            if (cur != ref) list[cur] = list[ref];
            ++ref;
        }
        list.resize(cur+1);
    }


    void merge_locked(std::chrono::system_clock::time_point tp) {
        if (!_cur_value) {
            create_snapshot(_bids, _asks, tp);
        } else {
            auto new_snp = alloc_snapshot();
            std::stable_sort(_bids.begin(), _bids.end(), &OrderBookLevel::order_bids);
            std::stable_sort(_asks.begin(), _asks.end(), &OrderBookLevel::order_asks);
            remove_dups(_bids);
            remove_dups(_asks);
            apply_increment(_cur_value->bids, _bids, new_snp->bids, &OrderBookLevel::order_bids);
            apply_increment(_cur_value->asks, _asks, new_snp->asks, &OrderBookLevel::order_asks);
            new_snp->time = tp;
            _cur_value = new_snp;
        }
        _bids.clear();
        _asks.clear();            


    }   


    PSnapshot alloc_snapshot() {        
        PSnapshot new_snp;
        if (_pool.empty()) {
            std::unique_ptr<Snapshot> s = std::make_unique<Snapshot>();
            s->owner = shared_from_this();
            new_snp = PSnapshot(s.release());
        } else {
            new_snp = std::move(_pool.front());
            _pool.pop();
        }
        return new_snp;

    }
    void create_snapshot(std::span<const OrderBookLevel> bids, std::span<const OrderBookLevel> asks, std::chrono::system_clock::time_point tp) {
        auto new_snp = alloc_snapshot();

        new_snp->asks.resize(asks.size());
        new_snp->bids.resize(bids.size());
        std::copy(bids.begin(), bids.end(), new_snp->bids.begin());
        std::copy(asks.begin(), asks.end(), new_snp->asks.begin());
        std::sort(new_snp->bids.begin(), new_snp->bids.end(), &OrderBookLevel::order_bids);
        std::sort(new_snp->asks.begin(), new_snp->asks.end(), &OrderBookLevel::order_asks);
        new_snp->time = tp;
        _cur_value = new_snp;

    }

    void release_snapshot(Snapshot *snp) {
        std::scoped_lock _(_mx);
        if (_pool.size() >= max_pool_size) delete snp;
        else _pool.push(PSnapshot(snp));
    }


};



}