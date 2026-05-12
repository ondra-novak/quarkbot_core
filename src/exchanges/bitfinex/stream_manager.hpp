#pragma once

#include "exchanges/bitfinex/public_stream.hpp"
#include "impl/streaming/lock_free_publisher.hpp"
#include "impl/streaming/publisher_manager.hpp"
#include "libs/network/sslobjects.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/stream_defs.hpp"
#include "ifc/streaming.hpp"
#include "utils/hashable.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>
namespace quarkbot {
namespace bitfinex{

    class StreamManager : public std::enable_shared_from_this<StreamManager>{
    public:

        StreamManager(network::PSSL_CTX sslctx, PExecutionWorker worker):_sslctx(std::move(sslctx)),_worker(worker) {}
        ~StreamManager();

        std::unique_ptr<IEventStreamBase> subscribe(std::string symbol, StreamTypeItem::Type type, const StreamParams *params);

        using TradeStream = LockFreePublisher<Trade, 1>;
        using QuoteStream = LockFreePublisher<Quote, 1>;
        using TradeCounterStream = LockFreePublisher<TradeCounter, 1>;
        using CloseBarStream = LockFreePublisher<TradeCounter, 1>;

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

        std::mutex _mx;
        std::vector<std::unique_ptr<PublicStream> > _streams;
        network::PSSL_CTX _sslctx;
        PExecutionWorker _worker;
        PublisherManager<std::string> _manager;
        std::unordered_set<StreamRegKey, Hasher<StreamRegKey> > _active_subscribtions;
        

        using TraderStreamMap = std::unordered_map<std::string, std::weak_ptr<TradeStream> >;
        using QuoteStreamMap = std::unordered_map<std::string, std::weak_ptr<QuoteStream> >;
        using TradeCounterStreamMap = std::unordered_map<std::string, std::weak_ptr<TradeCounterStream> >;
        using CloseBarStreamMap = std::unordered_map<std::string, std::weak_ptr<TradeCounterStream> >;
/*
        TraderStreamMap _trade_stream_map;
        QuoteStreamMap _quote_stream_map;
        TradeCounterStreamMap _trade_counter_stream_map;
        CloseBarStreamMap _close_bar_stream_map;
*/
        template<std::invocable<PublicStream &> Fn>
        void subscribe_to_stream(Fn &&fn);

        struct TradeParser {
            StreamManager *owner;
            std::string symbol;            
            bool operator()(const Json message);
        };
        struct TickerParser {
            StreamManager *owner;
            std::string symbol;
            bool operator()(const Json message);
        };
        void subscribe_trades_if_needed(const std::string &symbol);
        void subscribe_ticker_if_needed(const std::string &symbol);
        
        template<typename ... Map>
        auto find_streams(const std::string &symbol, Map &... maps);

        void subscribe_to_scream_bgr(std::function<PublicStream::State(PublicStream &)> fn);
    };

}
}