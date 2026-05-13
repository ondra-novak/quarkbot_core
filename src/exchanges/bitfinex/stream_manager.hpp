#pragma once

#include "basic_coro/cancel_signal.hpp"
#include "basic_coro/coroutine.hpp"
#include "exchanges/bitfinex/iprice_report.hpp"
#include "exchanges/bitfinex/network_context.hpp"
#include "exchanges/bitfinex/public_stream.hpp"
#include "impl/streaming/lock_free_publisher.hpp"
#include "impl/streaming/publisher_manager.hpp"
#include "libs/network/sslobjects.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/stream_defs.hpp"
#include "ifc/streaming.hpp"
#include "utils/hashable.hpp"
#include <chrono>
#include <coroutine>
#include <memory>
#include <unordered_map>
#include <unordered_set>
namespace quarkbot {
namespace bitfinex{

    class StreamManager : public std::enable_shared_from_this<StreamManager>{
    public:

        static unsigned int long_interval;

        using PriceReportCallback =  std::function<void(const std::string &, Decimal)>;

        StreamManager(NetworkContext  sslctx, PExecutionWorker worker)
            :_sslctx(std::move(sslctx)),_worker(worker) {}
        ~StreamManager();

        std::unique_ptr<IEventStreamBase> subscribe(std::string symbol, 
            StreamTypeItem::Type type, 
            const StreamParams *params, 
            std::weak_ptr<IPriceReport> reporter
        );

        struct OrderbookSnapshot {
            std::array<OrderBookLevel, 25> _bids;
            std::array<OrderBookLevel, 25> _asks;  
            std::chrono::system_clock::time_point _tp;          
            operator OrderBookView() {
                return OrderBookView(_bids, _asks, _tp);
            }
        };

        using TradeStream = LockFreePublisher<Trade, 1>;
        using QuoteStream = LockFreePublisher<Quote, 1>;
        using TradeCounterStream = LockFreePublisher<TradeCounter, 1>;
        using CloseBarStream = LockFreePublisher<TradeCounter, 1>;
        using OrderBookStream = LockFreePublisher<OrderBookView, 1>;
        using PeriodicSnapshotStream = LockFreePublisher<PeriodicSnapshotView, 1>;

    protected:
        enum class StreamType {
            trades,
            ticker
        };

        struct StreamRegKey {
            std::string instrument;
            StreamType type;

            bool operator==(const StreamRegKey &other) const = default;
            std::size_t get_hash() const {
                Hasher<std::string> h;
                return h(instrument)+static_cast<std::size_t>(type);
            }
        };

        struct PeriodicStreamRegKey {
            std::string instrument;
            unsigned int period;
            bool operator==(const PeriodicStreamRegKey &other) const = default;
            std::size_t get_hash() const {
                Hasher<std::string> h;
                return h(instrument)+static_cast<std::size_t>(period);
            }
        };        

        struct PeriodicStreamRegVal {
            std::weak_ptr<PeriodicSnapshotStream> stream;
            coro::cancel_signal _cancel = {};
            std::atomic<bool> _finished = {};
        
        };

        using BulkRestTicket = std::unordered_map<std::string, std::vector<awaitable<PeriodicSnapshotView>::result > >;
        using PBulkRestTicket = std::unique_ptr<BulkRestTicket>;
        
        std::mutex _mx;
        std::vector<std::unique_ptr<PublicStream> > _streams;
        NetworkContext _sslctx;
        PExecutionWorker _worker;
        PublisherManager<std::string> _manager;
        std::unordered_set<StreamRegKey, Hasher<StreamRegKey> > _active_subscribtions;
        std::unordered_map<PeriodicStreamRegKey,PeriodicStreamRegVal, Hasher<PeriodicStreamRegKey> > _active_periodic_subscriptions;        
        PBulkRestTicket _bulk_rest_ticker = {};

        static awaitable<PeriodicSnapshotView> request_ticker(std::shared_ptr<StreamManager> me, const std::string &name);
        static coro::coroutine<void> retrieve_ticker(std::shared_ptr<StreamManager> me);
        


            
//        std::weak_ptr<PeriodicSnapshotStream> > _periodic_snapshots;
        
        template<std::invocable<PublicStream &> Fn>
        void subscribe_to_stream(Fn &&fn);

        struct TradeParser {
            StreamManager *owner;
            std::string symbol;            
            std::weak_ptr<IPriceReport> price_report;
            bool operator()(const Json message);
        };
        struct TickerParser {
            StreamManager *owner;
            std::string symbol;
            bool operator()(const Json message);
        };
        struct OrderbookParser {
            StreamManager *owner;            
            std::string symbol;
            bool operator()(const Json message);
            void apply_increment(Decimal price, Decimal size);
        };
        void subscribe_trades_if_needed(const std::string &symbol, std::weak_ptr<IPriceReport> reporter);
        void subscribe_ticker_if_needed(const std::string &symbol);
        
        template<typename ... Map>
        auto find_streams(const std::string &symbol, Map &... maps);

        void subscribe_to_scream_bgr(std::function<PublicStream::State(PublicStream &)> fn);
        std::shared_ptr<PeriodicSnapshotStream> retrieve_periodic_stream(const std::string &id, unsigned int period);                
        static coro::coroutine<void> periodic_worker(std::weak_ptr<StreamManager> wkme, 
                    PExecutionWorker worker,
                    std::string symbol,
                    EventStream<PeriodicSnapshotView> sub,
                    unsigned int interval,
                    PeriodicStreamRegVal &reg                    
                );
    };

}
}