#include "stream_manager.hpp"
#include "exchanges/bitfinex/public_stream.hpp"
#include "market_instrument.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace quarkbot {
namespace bitfinex {

void StreamManager::subscribe_trades_if_needed(const std::string &symbol) {
    auto tsymbol = "t"+symbol;
    auto tsm = _trade_stream_map.find(symbol);
    auto tcsm = _trade_counter_stream_map.find(symbol);
    if (tsm ==  _trade_stream_map.end() && tcsm == _trade_counter_stream_map.end()) {
        _subscribe_queue.push([&](PublicStream &s){return s.subscribe_trades(tsymbol, TradeParser{this, symbol});});
    }
}
void StreamManager::subscribe_ticker_if_needed(const std::string &symbol) {
    auto tsymbol = "t"+symbol;
    auto qsm = _quote_stream_map.find(symbol);
    if (qsm ==  _quote_stream_map.end()) {
        _subscribe_queue.push([&](PublicStream &s){return s.subscribe_ticker(tsymbol, TickerParser{this, symbol});});
    }        
}

void StreamManager::run_queue() {
    std::scoped_lock _(_mx);
    while (!_subscribe_queue.empty()) {
        auto fn = std::move(_subscribe_queue.front());
        _subscribe_queue.pop();
        try {
            subscribe_to_stream(fn);        
        } catch (...) {
            _subscribe_queue.push(std::move(fn));            
            throw;
        }
    }         
}

std::unique_ptr<IEventStreamBase> StreamManager::subscribe(std::string symbol, StreamTypeItem::Type type, const StreamParams &) {
    std::scoped_lock _(_mx);
    if (type == Trade::type) {
        subscribe_trades_if_needed(symbol);
        auto &def = _trade_stream_map[symbol];        
        auto p = def.lock();
        if (!p) {
            def = p = std::make_shared<TradeStream>();
        }
        return p->create_subscriber(p);
    } 
    if (type == Quote::type) {
        subscribe_ticker_if_needed(symbol);
        auto &def = _quote_stream_map[symbol];
        auto p = def.lock();
        if (!p) {
            def = p = std::make_shared<QuoteStream>();
        }
        return p->create_subscriber(p);
    }
    if (type == TradeCounter::type) {
        subscribe_trades_if_needed(symbol);
        auto &def = _trade_counter_stream_map[symbol];        
        auto p = def.lock();
        if (!p) {
            def = p = std::make_shared<TradeCounterStream>();
        }
        return p->create_subscriber(p);
    }
    return {};
}

template<std::invocable<PublicStream &> Fn>
void StreamManager::subscribe_to_stream(Fn &&fn) {
    for (auto &x: _streams) {
        PublicStream::State st = fn(*x);
        if (st == PublicStream::ok) return;
        if (st == PublicStream::closed) {
            x.reset();
            x = std::make_unique<PublicStream>(_sslctx);
            st = fn(*x);
            if (st == PublicStream::ok) return;
        }
    }
    auto nw = std::make_unique<PublicStream>(_sslctx);
    auto st = fn(*nw);
    if (st != PublicStream::ok) throw std::runtime_error("Failed to create stream to bitfinex");
    _streams.push_back(std::move(nw));
}

template<typename ... Map>
auto StreamManager::find_streams(const std::string &symbol, Map &... maps) {
    std::scoped_lock _(_mx);
    
    auto srch = [&]<typename T>(T &mp) {
        using Ret = decltype(std::declval<T>()[std::declval<std::string>()].lock());
        auto itr = mp.find(symbol);
        if (itr == mp.end()) return Ret();
        Ret lk = itr->second.lock();
        if (!lk) {
            mp.erase(itr);
            return Ret();
        }
        return lk;
    };
    return std::make_tuple(srch(maps)...);
}


 StreamManager::~StreamManager() {
    std::vector<std::unique_ptr<PublicStream> > tmp;
    {
        std::scoped_lock _(_mx);
        tmp = std::move(_streams);
    }
    tmp.clear();
 }

bool  StreamManager::TradeParser::operator()(const Json message) {
    if (message.is_null()) {
        std::scoped_lock _(owner->_mx);
        owner->_subscribe_queue.push([&](PublicStream &x){return x.subscribe_trades(symbol, *this);});
        return false;
    } else {
        auto mts = std::chrono::system_clock::time_point(std::chrono::milliseconds(message[1].as<std::int64_t>()));
        auto amn = Decimal::from_string(message[2].as_text());
        auto price = Decimal::from_string(message[3].as_text());
        auto [ts,tcs] = owner->find_streams(symbol, owner->_trade_stream_map, owner->_trade_counter_stream_map);
        if (!ts && !tcs) return false;
        if (ts) {
            ts->publish(Trade{{},amn,price, mts});
        }
        if (tcs) {
            TradeCounter cntr = {};
            auto s = tcs->get_top_seq();
            if (s > 0) [[likely]] {
                --s;
                tcs->read(cntr,s);
            }
            cntr.last_price = price;
            cntr.volume += amn;
            cntr.trades++;
            cntr.time = mts;                
            tcs->publish(cntr);
        }
        return true;
    }

}
bool  StreamManager::TickerParser::operator()(const Json message) {
    if (message.is_null()) {
        std::scoped_lock _(owner->_mx);
        owner->_subscribe_queue.push([&](PublicStream &x){return x.subscribe_ticker(symbol, *this);});
        return false;
    } else {
        auto bid =  Decimal::from_string(message[0].as_text());
        auto bid_size =  Decimal::from_string(message[1].as_text());
        auto ask =  Decimal::from_string(message[2].as_text());
        auto ask_size =  Decimal::from_string(message[3].as_text());
        auto [qs] = owner->find_streams(symbol, owner->_quote_stream_map);
        if (!qs) return false;
        qs->publish(Quote{{},bid,bid_size,ask,ask_size, std::chrono::system_clock::now()});
        return true;
    }
}

}
}