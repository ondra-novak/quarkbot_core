#pragma once

#include "basic_coro/cancel_signal.hpp"
#include "basic_coro/coroutine.hpp"
#include "exchanges/bitfinex/iprice_report.hpp"
#include "exchanges/bitfinex/network_context.hpp"
#include "exchanges/bitfinex/public_stream.hpp"
#include "impl/streaming/lock_free_publisher.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/stream_defs.hpp"
#include "ifc/streaming.hpp"
#include "utils/hashable.hpp"
#include <chrono>
#include <memory>
#include <unordered_map>
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

            constexpr OrderbookSnapshot() {
                for (auto &x: _asks) {
                    x.price = Decimal::max();
                    x.size = 0;
                }
            }

            operator OrderBookView() noexcept {
                return OrderBookView(_bids, _asks, _tp);
            }
        };

        using TradeStream = LockFreePublisher<Trade, 1>;
        using QuoteStream = LockFreePublisher<Quote, 1>;
        using TradeCounterStream = LockFreePublisher<TradeCounter, 1>;
        using CloseBarStream = LockFreePublisher<TradeCounter, 1>;
        using PeriodicSnapshotStream = LockFreePublisher<PeriodicSnapshotView, 1>;
        class OrderBookStream : public LockFreePublisher<OrderbookSnapshot, 1> {
        public:
            OrderBookStream():LockFreePublisher<OrderbookSnapshot, 1>(
                [](std::shared_ptr<PublisherBase> this_shared) -> std::unique_ptr<IEventStreamBase> {
                    auto me = std::static_pointer_cast<OrderBookStream>(this_shared);
                    return std::make_unique<StreamSubscriber<OrderBookView, OrderBookStream> >(me);
                }) {}
        };

    protected:

        using TradeStreamMap = std::unordered_map<std::string, std::weak_ptr<TradeStream> >;
        using QuoteStreamMap = std::unordered_map<std::string, std::weak_ptr<QuoteStream> >;
        using TradeCounterStreamMap = std::unordered_map<std::string, std::weak_ptr<TradeCounterStream> >;
        using CloseBarStreamMap = std::unordered_map<std::string, std::weak_ptr<CloseBarStream> >;
        using OrderBookStreamMap = std::unordered_map<std::string, std::weak_ptr<OrderBookStream> >;
        using PeriodicSnapshotStreamMap = std::unordered_map<std::string, std::weak_ptr<PeriodicSnapshotStream> >;

        TradeStreamMap  _mapTradeStream;
        QuoteStreamMap  _mapQuoteStream;
        TradeCounterStreamMap  _mapTradeCounterStream;
        CloseBarStreamMap  _mapCloseBarStream;
        OrderBookStreamMap  _mapOrderBookStream;
        PeriodicSnapshotStreamMap  _mapPeriodicSnapshotStream;

        template<typename T>
        auto create_subscriber(T &map, const std::string &symbol);
        template<typename ... Map>
        auto find_streams(const std::string &symbol, Map &... maps);
        


        enum class StreamType {
            trades,
            ticker,
            orderbook
        };

        bool is_stream_active(const std::string &id, StreamType type) const;
        void subscribe_public_stream_if_needed(const std::string &id, StreamType type, std::weak_ptr<IPriceReport> rpt = {});


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
        void subscribe_orderbook_if_needed(const std::string &symbol);
        

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