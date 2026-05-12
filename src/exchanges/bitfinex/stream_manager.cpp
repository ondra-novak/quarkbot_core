#include "stream_manager.hpp"
#include "context.hpp"
#include "exchanges/bitfinex/public_stream.hpp"
#include "market_instrument.hpp"
#include "stream_defs.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace quarkbot {
namespace bitfinex {

void StreamManager::subscribe_trades_if_needed(const std::string &symbol) {
    auto tsymbol = "t"+symbol;
    auto ins = _active_subscribtions.emplace(StreamRegKey{symbol, StreamType::trades});
    if (ins.second) {
        subscribe_to_scream_bgr([=,this](PublicStream &s){return s.subscribe_trades(tsymbol, TradeParser{this, symbol});});
    }
}
void StreamManager::subscribe_ticker_if_needed(const std::string &symbol) {
    auto tsymbol = "t"+symbol;
    auto ins = _active_subscribtions.emplace(StreamRegKey{symbol, StreamType::ticker});
    if (ins.second) {
        subscribe_to_scream_bgr([=,this](PublicStream &s){return s.subscribe_ticker(tsymbol, TickerParser{this, symbol});});
    }        
}



std::unique_ptr<IEventStreamBase> StreamManager::subscribe(std::string symbol, StreamTypeItem::Type type, const StreamParams *params) {
    std::scoped_lock _(_mx);
    if (type == Trade::type) {
        subscribe_trades_if_needed(symbol);
        return _manager.connect_to(symbol,{},type, params, []{return std::make_shared<TradeStream>();});
    } 
    if (type == Quote::type) {
        subscribe_ticker_if_needed(symbol);
        return _manager.connect_to(symbol,{},type, params, []{return std::make_shared<QuoteStream>();});
    }
    if (type == TradeCounter::type) {
        subscribe_trades_if_needed(symbol);
        return _manager.connect_to(symbol,{},type, params, []{return std::make_shared<TradeCounterStream>();});    
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

    bool active = false;
    
    if (message.is_null()) {        
        owner->_manager.enum_all_publishers(symbol, {}, Trade::type, [&](auto, auto){active = true;});
        owner->_manager.enum_all_publishers(symbol, {}, TradeCounter::type, [&](auto, auto){active = true;});
        if (active) {
            std::scoped_lock _(owner->_mx);
            owner->subscribe_to_scream_bgr([=,this](PublicStream &x){return x.subscribe_trades(symbol, *this);});
        }
    } else {        
        auto type = message[1];
        if (!type.is_string() || type.as_text() != "te") return true;

        auto data = message[2];
        auto mts = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::milliseconds(
            static_cast<std::uint64_t>(data[1].as<double>()))));
        auto amn = Decimal::from_string(data[2].as_text());
        Side side = amn < 0?Side::sell:Side::buy;
        auto size = abs(amn);
        auto price = Decimal::from_string(data[3].as_text());
        owner->_manager.enum_all_publishers(symbol, {}, Trade::type, [&](auto , auto pub){
            active = true;
            auto ts = std::static_pointer_cast<TradeStream>(pub);
            ts->publish(Trade{{},price,size, mts, side});
        });
        owner->_manager.enum_all_publishers(symbol, {}, TradeCounter::type, [&](auto , auto pub){
            active = true;
            auto tcs = std::static_pointer_cast<TradeCounterStream>(pub);
            TradeCounter cntr = {};
            auto s = tcs->get_top_seq();
            if (s > 0) [[likely]] {
                --s;
                tcs->read(cntr,s);
            }
            cntr.last_price = price;
            cntr.volume += size;
            if (side == Side::sell) cntr.sell_volume+=size;
            if (side == Side::buy) cntr.buy_volume+=size;
            cntr.trades++;
            cntr.time = mts;                
            tcs->publish(cntr);
        });
    }
    if (!active) {
        std::scoped_lock _(owner->_mx);
        owner->_active_subscribtions.erase(StreamRegKey{symbol, StreamType::trades});
        return false;
    }
    return true;

}
bool  StreamManager::TickerParser::operator()(const Json message) {
    bool active = false;
    if (message.is_null()) {        
        owner->_manager.enum_all_publishers(symbol, {}, Quote::type, [&](auto, auto){active = true;});
        if (active) {
            std::scoped_lock _(owner->_mx);
            owner->subscribe_to_scream_bgr([=,this](PublicStream &x){return x.subscribe_ticker(symbol, *this);});
        }
    } else {
        Json data = message[1];
        if (!data.is_array()) return true;
        auto bid =  Decimal::from_string(data[0].as_text());
        auto bid_size =  Decimal::from_string(data[1].as_text());
        auto ask =  Decimal::from_string(data[2].as_text());
        auto ask_size =  Decimal::from_string(data[3].as_text());
        owner->_manager.enum_all_publishers(symbol, {}, Quote::type, [&](auto, auto pub){
            active = true;
            auto qs = std::static_pointer_cast<QuoteStream>(pub);
            qs->publish(Quote{{},bid,bid_size,ask,ask_size, std::chrono::system_clock::now()});
        });        
        return true;
    }
    if (!active) {
        std::scoped_lock _(owner->_mx);
        owner->_active_subscribtions.erase(StreamRegKey{symbol, StreamType::ticker});
        return false;
    }
    return true;
}

void  StreamManager::subscribe_to_scream_bgr(std::function<PublicStream::State(PublicStream &)> fn) {
    auto me = shared_from_this();
    auto coro = [](auto me, auto fn) -> StrategyFragment{        
        me->subscribe_to_stream(std::move(fn));                
        co_return;
    };
    _worker->run(coro(me, std::move(fn)));
}

}
}