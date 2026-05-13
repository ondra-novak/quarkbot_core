#include "stream_manager.hpp"
#include "basic_coro/coroutine.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "context.hpp"
#include "exchanges/bitfinex/iprice_report.hpp"
#include "exchanges/bitfinex/public_stream.hpp"
#include "libs/network/http_status_exception.hpp"
#include "libs/network/rest.hpp"
#include "market_instrument.hpp"
#include "stream_defs.hpp"
#include "streaming.hpp"
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace quarkbot {
namespace bitfinex {

unsigned int StreamManager::long_interval = 60;

bool StreamManager::is_stream_active(const std::string &id, StreamType type) const {
    switch (type) {
        case StreamType::orderbook: 
            return _mapOrderBookStream.find(id) != _mapOrderBookStream.end();
        case StreamType::ticker:
            return _mapQuoteStream.find(id) != _mapQuoteStream.end()
                || _mapPeriodicSnapshotStream.find(id) != _mapPeriodicSnapshotStream.end();
        case StreamType::trades:
            return _mapTradeStream.find(id) != _mapTradeStream.end()
                || _mapTradeCounterStream.find(id) != _mapTradeCounterStream.end();
    }
}

void StreamManager::subscribe_public_stream_if_needed(const std::string &symbol, StreamType type, std::weak_ptr<IPriceReport> rpt ) {
    if (is_stream_active(symbol, type)) return;
    std::function<PublicStream::State(PublicStream &)> fn;
    std::string tsymbol = "t"+symbol;
    
    switch (type) {
        case StreamType::orderbook:
            fn = [=,this](PublicStream &s){
                return s.subscribe_orderbook(tsymbol, OrderbookParser{this, symbol});
            };
            break;
        case StreamType::ticker:
            fn = [=,this](PublicStream &s){
                return s.subscribe_ticker(tsymbol, TickerParser{this, symbol});
            };
            break;
        case StreamType::trades:
            fn = [=,this](PublicStream &s){
                return s.subscribe_trades(tsymbol,TradeParser{this, symbol, std::move(rpt)});
            };
            break;                        
        default:
            return;
    }
    subscribe_to_scream_bgr(std::move(fn));;
}

template<typename T>
auto StreamManager::create_subscriber(T &map, const std::string &symbol) {
    auto &ref = map[symbol];
    auto pub = ref.lock();
    if (!pub) {
        pub = std::make_shared<typename T::mapped_type::element_type>();
        ref = pub;
    }
    return pub->create_subscriber(pub);
}



std::unique_ptr<IEventStreamBase> StreamManager::subscribe(std::string symbol, StreamTypeItem::Type type, 
    const StreamParams *params, std::weak_ptr<IPriceReport> reporter) {
    std::unique_ptr<IEventStreamBase> out;
    std::scoped_lock _(_mx);
    if (type == Trade::type) {    
        subscribe_public_stream_if_needed(symbol, StreamType::trades,std::move(reporter));
        out = create_subscriber(_mapTradeStream, symbol);
    } 
    else if (type == Quote::type) {        
        subscribe_public_stream_if_needed(symbol, StreamType::ticker);
        out = create_subscriber(_mapQuoteStream, symbol);
    }
    else if (type == TradeCounter::type) {
        subscribe_public_stream_if_needed(symbol, StreamType::trades, std::move(reporter));
        out = create_subscriber(_mapTradeCounterStream, symbol);
    }
    else if (type == PeriodicSnapshotView::type) {       
        auto publisher = retrieve_periodic_stream(symbol, static_cast<const PeriodicSnapshotView::ParamType *>(params)->param);
        out =  publisher->create_subscriber(publisher);
    }
    else if (type == OrderBook<25>::type) {
        subscribe_public_stream_if_needed(symbol, StreamType::orderbook);
        out = create_subscriber(_mapOrderBookStream, symbol);
    }
    return out;
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
    for (auto &[k,v]:_active_periodic_subscriptions) {
        _worker->cancel(&v._cancel);
    }
    for (auto &[k,v]:_active_periodic_subscriptions) {
        v._finished.wait(false);
    }
 }

bool  StreamManager::TradeParser::operator()(const Json message) {

    auto [ts, tc] = owner->find_streams(symbol, owner->_mapTradeStream, owner->_mapTradeCounterStream);
    bool active = ts || tc;
    if (message.is_null()) {        
        if (active) {
            std::scoped_lock _(owner->_mx);
            owner->subscribe_to_scream_bgr([=,this](PublicStream &x){return x.subscribe_trades(symbol, *this);});            
        }
    } else {
        auto type = message[1];
        if (!type.is_string() || type.as_text() != "te") return active;

        auto data = message[2];
        auto mts = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::milliseconds(
            static_cast<std::uint64_t>(data[1].as<double>()))));
        auto amn = Decimal::from_string(data[2].as_text());
        Side side = amn < 0?Side::sell:Side::buy;
        auto size = abs(amn);
        auto price = Decimal::from_string(data[3].as_text());
        if (ts) ts->publish(Trade{{},price,size, mts, side});
        if (tc) {
            TradeCounter cntr = {};
            auto s = tc->get_top_seq();
            if (s > 0) [[likely]] {
                --s;
                tc->read(cntr,s);
            }
            cntr.last_price = price;
            cntr.volume += size;
            if (side == Side::sell) cntr.sell_volume+=size;
            if (side == Side::buy) cntr.buy_volume+=size;
            cntr.trades++;
            cntr.time = mts;                
            tc->publish(cntr);
        }
    }
    return active;
}
bool  StreamManager::TickerParser::operator()(const Json message) {
    auto [qs, pss] = owner->find_streams(symbol, owner->_mapQuoteStream, owner->_mapPeriodicSnapshotStream);
    bool active = qs || pss;
    if (message.is_null()) {        
        if (active) {
            std::scoped_lock _(owner->_mx);
            owner->subscribe_to_scream_bgr([=,this](PublicStream &x){return x.subscribe_ticker(symbol, *this);});
        }
    } else {
        Json data = message[1];
        if (!data.is_array()) return active;
        auto bid =  Decimal::from_string(data[0].as_text());
        auto bid_size =  Decimal::from_string(data[1].as_text());
        auto ask =  Decimal::from_string(data[2].as_text());
        auto ask_size =  Decimal::from_string(data[3].as_text());
        auto last = Decimal::from_string(data[6].as_text());
        auto now = std::chrono::system_clock::now();

        if (qs) {
            qs->publish(Quote{{},bid,bid_size,ask,ask_size, now});
        }
        if (pss) {
            pss->publish(PeriodicSnapshotView{{{},bid,bid_size,ask,ask_size, now},last});
        }
    }
    return active;
}

void  StreamManager::subscribe_to_scream_bgr(std::function<PublicStream::State(PublicStream &)> fn) {
    auto me = shared_from_this();
    auto coro = [](auto me, auto fn) -> StrategyFragment{        
        me->subscribe_to_stream(std::move(fn));                
        co_return;
    };
    _worker->run(coro(me, std::move(fn)));
}

coro::coroutine<void> StreamManager::periodic_worker(std::weak_ptr<StreamManager> wkme, 
            PExecutionWorker worker,
            std::string symbol,
            EventStream<PeriodicSnapshotView> sub,
            unsigned int interval,            
            PeriodicStreamRegVal &reg) {


        auto now =  worker->now();
        while (true) {
            auto prev = std::chrono::system_clock::from_time_t((std::chrono::system_clock::to_time_t(now)/interval)*interval);
            auto nxt = prev + std::chrono::seconds(interval);
            {
                bool st = co_await worker->sleep_until(nxt, &reg._cancel);
                if (!st) {
                     break;
                }
            }
            now = worker->now();
            PeriodicSnapshotView curval;
            auto pub = reg.stream.lock();
            if (!pub) {
                break;
            }

            bool available = false;
            if (sub) {
                if (sub.current(curval)) { //false when no value is available
                    available = true;                    
                }
            } 
            if (!available) {
                auto me = wkme.lock();
                if (me)  {            
                    auto [pss] = me->find_streams(symbol, me->_mapPeriodicSnapshotStream);
                    if (pss) {
                        auto seq = pss->get_top_seq();
                        if (seq) {
                            --seq;
                            pss->read(curval, seq);
                            available = true;
                        }
                    }
                    if (!available) {
                        try {
                            curval = co_await me->request_ticker(me, symbol);
                            available = true;                        
                        } catch (...) {
                            // ignor error
                        }
                    }

                }
            }
            if (available) {
                pub->publish(curval);
            }

        }
        reg._finished = true;
        reg._finished.notify_all();

    }

std::shared_ptr< StreamManager::PeriodicSnapshotStream>  StreamManager::retrieve_periodic_stream(const std::string &id, unsigned int period) {    
    PeriodicStreamRegKey key{id,period};
    auto iter = _active_periodic_subscriptions.find(key);
    std::shared_ptr< StreamManager::PeriodicSnapshotStream> pub;
    if (iter != _active_periodic_subscriptions.end()) {
        pub = iter->second.stream.lock();
        if (pub)  return pub;
        _active_periodic_subscriptions.erase(iter);
    }    
    PeriodicStreamRegVal &v = _active_periodic_subscriptions[key];
    std::unique_ptr<IEventStreamBase> sub;
    if (period < long_interval) {
        subscribe_public_stream_if_needed(id, StreamType::ticker);
        sub = create_subscriber(_mapPeriodicSnapshotStream,id);
    } else {
        sub = std::make_unique<IEventStream<PeriodicSnapshotView>::Null>();
    }
    pub = std::make_shared<PeriodicSnapshotStream>();
    v.stream =pub;
    _worker->run(periodic_worker(weak_from_this(),_worker, id, 
        EventStream<PeriodicSnapshotView>(std::move(sub)), period, v));    
    return pub;
}

coro::coroutine<void> StreamManager::retrieve_ticker(std::shared_ptr<StreamManager> me) {
    co_await me->_worker->sleep_for(std::chrono::milliseconds(50));
    auto ticket = std::move(me->_bulk_rest_ticker);
    me->_bulk_rest_ticker.reset();
    std::string rq = "/tickers?symbols=";
    for (auto &[k,v]: *ticket) {
        rq.push_back('t');
        rq.append(k);
        rq.push_back(',');
    }
    rq.pop_back();
    auto rest = me->_sslctx.create_rest();
    rest.add_header("Accept", "application/json");
    auto response = rest.GET(rq);
    if (response.code != 200) {
        auto exp = std::make_exception_ptr(network::HttpStatusException(response.code, response.message, response.read_body()));
        for (auto &[k, v]: *ticket) {
            for (auto &p: v) p(exp);
        }
    } else {        
        Json tickers = Json::parse(response.read_body_as_charstream());
        for (Json ticker: tickers.as_array()) {
            auto s = ticker[0].as_text().substr(1);
            PeriodicSnapshotView w {
                {{},
                Decimal::from_string(ticker[1].as_text()),
                Decimal::from_string(ticker[2].as_text()),
                Decimal::from_string(ticker[3].as_text()),
                Decimal::from_string(ticker[4].as_text()),
                me->_worker->now()},
                Decimal::from_string(ticker[6].as_text()),
            };
            auto iter = ticket->find(std::string(s));
            if (iter != ticket->end()) {
                for (auto &p: iter->second) {
                    p(w);
                }
            }            
        }
    }
}

awaitable<PeriodicSnapshotView> StreamManager::request_ticker(std::shared_ptr<StreamManager> me, const std::string &name) {
    return [me,name](auto promise) -> coro::prepared_coro {
        if (!me->_bulk_rest_ticker) {
            me->_bulk_rest_ticker = std::make_unique<BulkRestTicket>();
            (*me->_bulk_rest_ticker)[name].push_back(std::move(promise));            
            return coro::prepared_coro(retrieve_ticker(me).release());
        } else {
            auto &x = *me->_bulk_rest_ticker;
            x[name].push_back(std::move(promise));
            return {};
        }
    };


}
 
bool StreamManager::OrderbookParser::operator()(const Json message) {
    auto [ob] = owner->find_streams(symbol, owner->_mapOrderBookStream);
    bool active = !!ob;
    if (message.is_null()) {        
        if (active) {
            std::scoped_lock _(owner->_mx);
            owner->subscribe_to_scream_bgr([=,this](PublicStream &x){return x.subscribe_orderbook(symbol, *this);});
        }
     } else {
        Json data = message[1];
        if (!data.is_array()) return active;
        if (data[0].is_array()) { 
            //snapshot
            for (const auto &x: data[0].as_array()) {
                Decimal price = Decimal::from_string(x[1].as_text());
                Decimal size = Decimal::from_string(x[2].as_text());
                apply_increment(price, size);
            }
        } else{
            Decimal price = Decimal::from_string(data[1].as_text());
            Decimal size = Decimal::from_string(data[2].as_text());
            apply_increment(price, size);

        }
        //todo
    }
    return active;
  
}

void StreamManager::OrderbookParser::apply_increment(Decimal price, Decimal size) {
    //todo
}

}
}