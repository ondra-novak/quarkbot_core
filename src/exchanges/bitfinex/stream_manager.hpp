#pragma once

#include "defs.hpp"
#include "exchanges/bitfinex/public_stream.hpp"
#include "execution_worker.hpp"
#include "impl/streaming/lock_free_publisher.hpp"
#include "libs/network/sslobjects.hpp"
#include "market_instrument.hpp"
#include "stream_defs.hpp"
#include "streaming.hpp"
#include <memory>
#include <unordered_map>
namespace quarkbot {
namespace bitfinex{

    class StreamManager : public std::enable_shared_from_this<StreamManager>{
    public:

        StreamManager(network::PSSL_CTX sslctx):_sslctx(std::move(sslctx)) {}
        ~StreamManager();

        std::unique_ptr<IEventStreamBase> subscribe(std::string symbol, StreamTypeItem::Type type, const StreamParams &params);
        //throws exception with any error, however error operation is scheduled to retry
        void run_queue();

        using TradeStream = LockFreePublisher<Trade, 1>;
        using QuoteStream = LockFreePublisher<Quote, 1>;
        using TradeCounterStream = LockFreePublisher<TradeCounter, 1>;
        using CloseBarStream = LockFreePublisher<TradeCounter, 1>;

    protected:
        std::mutex _mx;
        std::vector<std::unique_ptr<PublicStream> > _streams;
        network::PSSL_CTX _sslctx;

        using TraderStreamMap = std::unordered_map<std::string, std::weak_ptr<TradeStream> >;
        using QuoteStreamMap = std::unordered_map<std::string, std::weak_ptr<QuoteStream> >;
        using TradeCounterStreamMap = std::unordered_map<std::string, std::weak_ptr<TradeCounterStream> >;
        using CloseBarStreamMap = std::unordered_map<std::string, std::weak_ptr<TradeCounterStream> >;

        TraderStreamMap _trade_stream_map;
        QuoteStreamMap _quote_stream_map;
        TradeCounterStreamMap _trade_counter_stream_map;
        CloseBarStreamMap _close_bar_stream_map;
        std::queue<std::function<PublicStream::State(PublicStream &x)> > _subscribe_queue;                        

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
    };

}
}