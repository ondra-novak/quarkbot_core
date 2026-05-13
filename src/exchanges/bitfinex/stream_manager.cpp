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

void StreamManager::subscribe_trades_if_needed(const std::string &symbol, std::weak_ptr<IPriceReport> reporter) {
    auto tsymbol = "t"+symbol;
    auto ins = _active_subscribtions.emplace(StreamRegKey{symbol, StreamType::trades});
    if (ins.second) {
        subscribe_to_scream_bgr([=,this](PublicStream &s){
            return s.subscribe_trades(tsymbol, TradeParser{this, symbol, reporter});
        });
    }
}
void StreamManager::subscribe_ticker_if_needed(const std::string &symbol) {
    auto tsymbol = "t"+symbol;
    auto ins = _active_subscribtions.emplace(StreamRegKey{symbol, StreamType::ticker});
    if (ins.second) {
        subscribe_to_scream_bgr([=,this](PublicStream &s){return s.subscribe_ticker(tsymbol, TickerParser{this, symbol});});
    }        
}



std::unique_ptr<IEventStreamBase> StreamManager::subscribe(std::string symbol, StreamTypeItem::Type type, 
    const StreamParams *params, std::weak_ptr<IPriceReport> reporter) {
    std::unique_ptr<IEventStreamBase> out;
    std::scoped_lock _(_mx);
    if (type == Trade::type) {    
        out =  _manager.connect_to(symbol,{},type, params, []{return std::make_shared<TradeStream>();});
        subscribe_trades_if_needed(symbol, reporter);
    } 
    if (type == Quote::type) {        
        out =  _manager.connect_to(symbol,{},type, params, []{return std::make_shared<QuoteStream>();});
        subscribe_ticker_if_needed(symbol);
    }
    if (type == TradeCounter::type) {
        out =  _manager.connect_to(symbol,{},type, params, []{return std::make_shared<TradeCounterStream>();});    
        subscribe_trades_if_needed(symbol, reporter);

    }
    if (type == PeriodicSnapshotView::type) {       
        auto publisher = retrieve_periodic_stream(symbol, static_cast<const PeriodicSnapshotView::ParamType *>(params)->param);
        out =  publisher->create_subscriber(publisher);
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

    bool active = false;
    
    if (message.is_null()) {        
        if (owner->_manager.any_publisher(symbol, {}, Trade::type)
            || owner->_manager.any_publisher(symbol, {}, TradeCounter::type)) {
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
        auto lk = price_report.lock();
        if (lk) {
            lk->report_price(symbol, price);
        }
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
        if (owner->_manager.any_publisher(symbol, {}, Quote::type)) {
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
        auto last = Decimal::from_string(data[6].as_text());
        auto now = std::chrono::system_clock::now();

        owner->_manager.enum_all_publishers(symbol, {}, Quote::type, [&](auto, auto pub){
            active = true;
            auto qs = std::static_pointer_cast<QuoteStream>(pub);
            qs->publish(Quote{{},bid,bid_size,ask,ask_size, now});
        });        
        owner->_manager.enum_all_publishers(symbol,{}, PeriodicSnapshotView::type,  [&](auto, auto pub){
            active = true;
            auto tk = std::static_pointer_cast<PeriodicSnapshotStream>(pub);
            tk->publish(PeriodicSnapshotView{{{},bid,bid_size,ask,ask_size, now},last});
        });     
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
                    me->_manager.enum_all_publishers(symbol, {}, PeriodicSnapshotView::type, [&](auto, auto srcpub){
                        auto mypub = std::static_pointer_cast<PeriodicSnapshotStream>(srcpub);
                        auto seq = mypub->get_top_seq();
                        if (seq) {
                            --seq;
                            mypub->read(curval, seq);
                            available = true;
                        }
                    });
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
        sub = _manager.connect_to(id, {}, PeriodicSnapshotView::type, nullptr, []{return std::make_shared<PeriodicSnapshotStream>();});
        subscribe_ticker_if_needed(id);
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
    bool active = false;
    if (message.is_null()) {        
        if (owner->_manager.any_publisher(symbol, {}, OrderBook<25>::type)) {
            std::scoped_lock _(owner->_mx);
            owner->subscribe_to_scream_bgr([=,this](PublicStream &x){return x.subscribe_orderbook(symbol, *this);});
        }
     } else {
        Json data = message[1];
        if (!data.is_array()) return true;
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
    if (!active) {
        std::scoped_lock _(owner->_mx);
        owner->_active_subscribtions.erase(StreamRegKey{symbol, StreamType::ticker});
        return false;
    }
    return true;
  
}

void StreamManager::OrderbookParser::apply_increment(Decimal price, Decimal size) {
    //todo
}

}
}