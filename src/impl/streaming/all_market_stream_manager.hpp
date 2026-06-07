#pragma once

#include "ifc/stream/closedbar.hpp"
#include "ifc/stream/quote.hpp"
#include "ifc/stream/rangedbar.hpp"
#include "ifc/stream/trade.hpp"
#include "ifc/stream/tradestat.hpp"
#include "ifc/stream/ticker.hpp"
#include "impl/streaming/closed_bar_lambda.hpp"
#include "impl/streaming/lock_free_publisher.hpp"
#include "impl/streaming/ranged_bar_lambda.hpp"
#include "impl/streaming/stream_maps.hpp"
#include "utils/class_hash.hpp"
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
    

    using QuoteStreamMap = SingleStreamMap<InstrumentRef, QuotePublisher>;
    using TradeStreamMap = SingleStreamMap<InstrumentRef, TradePublisher>;
    using TradeCounterStreamMap = SingleStreamMap<InstrumentRef, TradeCounterPublisher>;
    using TickerStreamMap = SingleStreamMap<InstrumentRef, TickerPublisher>;
    using ClosedBarStreamMap = ParametrizedStreamMap<InstrumentRef, ClosedBarPublisher, ClosedBar::Param>;
    using RangedBarStreamMap = ParametrizedStreamMap<InstrumentRef, RangedBarPublisher, RangedBar::Param>;

    std::unique_ptr<IEventStreamBase> subscribe_stream(const InstrumentRef &instrument, std::size_t type, const void *param) {
        switch (type) {
            case class_hash<Quote>: return _quotes.create_subscriber(instrument);
            case class_hash<Trade>: return _trades.create_subscriber(instrument);
            case class_hash<TradeStatCounter>: return _trade_stats.create_subscriber(instrument);
            case class_hash<Ticker>: return _tickers.create_subscriber(instrument);
            case class_hash<ClosedBar>: return _closed_bars.create_subscriber(instrument, *reinterpret_cast<const ClosedBar::Param *>(param));
            case class_hash<RangedBar>: return _ranged_bars.create_subscriber(instrument, *reinterpret_cast<const RangedBar::Param *>(param));
            default: return {};
        }
    }

    bool on_event(const InstrumentRef &instrument, Quote qt) {
        bool b1 = _quotes.with_publisher(instrument, [&](auto &pub){pub.publish(qt);});
        bool b2 = _tickers.with_publisher(instrument,[&](auto &pub){pub.publish(pub.get_top_value_ref().add(qt));});
        bool b3 = _closed_bars.enum_publisher(instrument,calculate_closed_bar(qt));
        return b1||b2||b3;
    }

    bool on_event(const InstrumentRef &instrument, Trade tr) {
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
        } 
    }


protected:
    QuoteStreamMap _quotes;
    TradeStreamMap _trades;
    TickerStreamMap _tickers;
    TradeCounterStreamMap _trade_stats;
    ClosedBarStreamMap _closed_bars;
    RangedBarStreamMap _ranged_bars;
 

};

}