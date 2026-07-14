#pragma once

#include "quarkbot/async.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/market_instrument.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/snapshot.hpp"
#include "quarkbot/stream/trade.hpp"
#include <stop_token>
namespace quarkbot {

///Implement receiving snapshot by using subscription to two streams
inline Async<bool> receive_snapshot_from_streams(MarketInstrument instrument, Snapshot &v, std::stop_token stopper) {
    EventStream<Trade> trades = instrument.subscribe<Trade>().stop_on(stopper);
    EventStream<Quote> quotes = instrument.subscribe<Quote>().stop_on(stopper);
    Trade tr;
    Quote qt;
    if (co_await trades.receive(tr) && co_await quotes.receive(qt)) {
        Quote &vq = v;
        vq = qt;
        v.last_price = tr.price;
        v.last_price_timestamp = tr.time;
        co_return true;
    }
    co_return false;
    
}


}