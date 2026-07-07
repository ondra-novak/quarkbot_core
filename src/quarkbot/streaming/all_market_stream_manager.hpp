#pragma once

#include "quarkbot/stream/auction.hpp"
#include "quarkbot/stream/closedbar.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/rangedbar.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/stream/tradestat.hpp"
#include "quarkbot/stream/ticker.hpp"
#include "closed_bar_lambda.hpp"
#include "lock_free_publisher.hpp"
#include "ranged_bar_lambda.hpp"
#include "stream_maps.hpp"
#include "quarkbot/hash/class_hash.hpp"
namespace quarkbot {

template<typename InstrumentRef>
class AllMarketStreamManager {
public:

    using QuotePublisher = LockFreePublisher<Quote, 1>;
    using TradePublisher = LockFreePublisher<Trade, 1>;
    using ClosedBarPublisher = LockFreePublisher<ClosedBar, 1>;
    using TradeCounterPublisher = LockFreePublisher<TradeStatCounter, 1>;
    using TickerPublisher = LockFreePublisher<Ticker, 1>;
    using RangedBarPublisher = LockFreePublisher<RangedBar, 1>;
    using AuctionPublisher = LockFreePublisher<Auction, 1>;
    

    using QuoteStreamMap = SingleStreamMap<InstrumentRef, QuotePublisher>;
    using TradeStreamMap = SingleStreamMap<InstrumentRef, TradePublisher>;
    using TradeCounterStreamMap = SingleStreamMap<InstrumentRef, TradeCounterPublisher>;
    using TickerStreamMap = SingleStreamMap<InstrumentRef, TickerPublisher>;
    using ClosedBarStreamMap = ParametrizedStreamMap<InstrumentRef, ClosedBarPublisher, ClosedBar::Param>;
    using RangedBarStreamMap = ParametrizedStreamMap<InstrumentRef, RangedBarPublisher, RangedBar::Param>;
    using AuctionStreamMap  = SingleStreamMap<InstrumentRef, AuctionPublisher>;

    std::shared_ptr<IEventStreamBase> subscribe_stream(const InstrumentRef &instrument, std::size_t type, const void *param) {
        switch (type) {
            case class_hash<Quote>: return _quotes.create_subscriber(instrument);
            case class_hash<Trade>: return _trades.create_subscriber(instrument);
            case class_hash<TradeStatCounter>: return _trade_stats.create_subscriber(instrument);
            case class_hash<Ticker>: return _tickers.create_subscriber(instrument);
            case class_hash<ClosedBar>: return _closed_bars.create_subscriber(instrument, *reinterpret_cast<const ClosedBar::Param *>(param));
            case class_hash<RangedBar>: return _ranged_bars.create_subscriber(instrument, *reinterpret_cast<const RangedBar::Param *>(param));
            case class_hash<Auction>: return _auctions.create_subscriber(instrument);
            default: return {};
        }
    }

    bool on_event(const InstrumentRef &instrument, const Quote &qt) {
        bool b1 = _quotes.with_publisher(instrument, [&](auto &pub){pub.publish(qt);});
        bool b2 = _tickers.with_publisher(instrument,[&](auto &pub){pub.publish(pub.get_top_value_ref().add(qt));});
        bool b3 = _closed_bars.enum_publisher(instrument,calculate_closed_bar(qt));
        return b1||b2||b3;
    }

    bool on_event(const InstrumentRef &instrument, const Auction &au) {
        bool b1 = _auctions.with_publisher(instrument, [&](auto &pub){pub.publish(au);});
        return b1;
    }

    bool on_event(const InstrumentRef &instrument, const Trade &tr) {
        bool b1 = _trades.with_publisher(instrument, [&](auto &pub){pub.publish(tr);});
        bool b2 = _trade_stats.with_publisher(instrument, [&](auto &pub){pub.publish(pub.get_top_value_ref().add(tr));});
        bool b3 = _tickers.with_publisher(instrument, [&](auto &pub){pub.publish(pub.get_top_value_ref().add(tr));});
        bool b4 = _closed_bars.enum_publisher(instrument,calculate_closed_bar(tr));
        bool b5 = _ranged_bars.enum_publisher(instrument,calculate_ranged_bar(tr));        
        return b1 || b2 || b3 || b4 || b5;
    }
    
    template<typename T>
    void collect_active(std::unordered_set<InstrumentRef> &refmap) {
        if constexpr(std::is_same_v<Trade, T> ) {
            _quotes.collect_active(refmap);
            _tickers.collect_active(refmap);
            _closed_bars.collect_active(refmap);
        } else if constexpr(std::is_same_v<Trade, T> ) {
            _trades.collect_active(refmap);
            _trade_stats.collect_active(refmap);
            _tickers.collect_active(refmap);
            _closed_bars.collect_active(refmap);
            _ranged_bars.collect_active(refmap);
        } else if constexpr(std::is_same_v<Auction, T> ) {
            _auctions.collect_active(refmap);
        }
    }


protected:
    QuoteStreamMap _quotes;
    TradeStreamMap _trades;
    TickerStreamMap _tickers;
    TradeCounterStreamMap _trade_stats;
    ClosedBarStreamMap _closed_bars;
    RangedBarStreamMap _ranged_bars;
    AuctionStreamMap _auctions;
 

};

}