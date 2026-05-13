#include "check.h"

#include "exchanges/bitfinex/instrument_map.hpp"
#include "exchanges/bitfinex/exchange.hpp"
#include "exchanges/bitfinex/network_context.hpp"
#include "exchanges/bitfinex/stream_manager.hpp"
#include "ifc/context.hpp"
#include "ifc/hub.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/types.hpp"
#include "ifc/underlying.hpp"
#include "impl/thread_executor.hpp"
#include "libs/network/rest.hpp"
#include "libs/network/sslobjects.hpp"
#include <chrono>
#include <future>
#include <memory>

using namespace quarkbot;
using namespace quarkbot::bitfinex;

NetworkContext ctx(network::ssl_init_client());


void test_load_currencies() {
    InstrumentMap map(ctx);
    auto list = map.get_all_currencies(nullptr);
    CHECK(!list.empty());
    //list can vary - check for BTC, USDT, and futures USDT (with correct transformation)
    auto iter = std::find(list.begin(), list.end(), UnderlyingCurrency{"BTC","XTB", nullptr});
    CHECK(iter != list.end());
    iter = std::find(list.begin(), list.end(), UnderlyingCurrency{"UST","USDt", nullptr});
    CHECK(iter != list.end());
    iter = std::find(list.begin(), list.end(), UnderlyingCurrency{"USTF0","USDt", nullptr});
    CHECK(iter != list.end());
    iter = std::find(list.begin(), list.end(), UnderlyingCurrency{"USD","USD", nullptr});
    CHECK(iter != list.end());
    iter = std::find(list.begin(), list.end(), UnderlyingCurrency{"WBT","", nullptr});
    CHECK(iter != list.end());
}

void test_exchange_object() {
    auto exchange = std::make_shared<Exchange>(ctx, ThreadExecutor::create());
    auto i1 = exchange->create_instrument("BTCUSD", InstrumentType::margin);
    auto info = i1->get_info();
    CHECK_EQUAL(info.name, "BTCUSD");
    CHECK_EQUAL(info.is_leveraged(), true);
    CHECK(info.leverage == 10); //adjust if leverage changes on bitfinex
    auto cur = info.quote_currency;
    CHECK_EQUAL(cur.unified_id, "USD");
    CHECK_EQUAL(cur.id, "USD");
    CHECK(cur.exchange == exchange.get());
    CHECK(info.type == InstrumentType::margin);
    CHECK_GREATER(info.lot_size_increment , 0);
    CHECK_GREATER(info.price_increment , 0);
    CHECK_GREATER(info.min_lot_size, 0);
    CHECK_EQUAL(info.tick_scale ,1);
    CHECK_EQUAL(info.multiplier ,1);
}


StrategyFragment test_stream_trades_frag(EventStream<Trade> stream, Hub<bool> &h) {
    auto now = std::chrono::system_clock::now() - std::chrono::seconds(10); //just window
    Trade tr;
    Trade tr2;
    bool r = co_await stream.next(tr);
    CHECK(r);
    CHECK(now< tr.time);    

    r = co_await stream.next(tr2);
    CHECK(r);
    CHECK(now< tr.time);
    CHECK(tr != tr2);
    CHECK_BETWEEN(1, tr.price , 10000000_dec);    
    Decimal ratio = tr.price/tr2.price;
    CHECK_BETWEEN(0.9_dec, ratio, 1.1_dec);

    stream.close();
    r = co_await stream.next(tr);
    CHECK(!r);
    co_await h.push(true);

}

void test_stream_trades() {
    auto executor = ThreadExecutor::create();
    auto exchange = std::make_shared<Exchange>(ctx, executor);
    auto instrument = exchange->create_instrument("BTCUSD", InstrumentType::margin);
    auto stream = instrument->subscribe<Trade>();    
    Hub<bool> hub;
    executor->run(test_stream_trades_frag(std::move(stream),hub));
    hub.pop().get();   
}   

StrategyFragment test_stream_quotes_frag(EventStream<Quote> stream, Hub<bool> &h) {
    auto now = std::chrono::system_clock::now() - std::chrono::seconds(10); //just window
    Quote qt;
    Quote qt2;
    bool r = co_await stream.next(qt);
    CHECK(r);
    CHECK(now< qt.time);    
    CHECK_LESS(qt.bid, qt.ask);
    CHECK_GREATER(qt.bid_size, 0);
    CHECK_GREATER(qt.ask_size, 0);

    CHECK_BETWEEN(1, qt.bid , 10000000_dec);
    CHECK_BETWEEN(1, qt.ask , 10000000_dec);

    r = co_await stream.next(qt2);
    CHECK(r);
    CHECK(now< qt.time);
    CHECK(qt != qt2);
    Decimal ratio = qt.bid/qt2.bid;
    CHECK_BETWEEN(0.9_dec, ratio, 1.1_dec);

    stream.close();
    r = co_await stream.next(qt);
    CHECK(!r);
    co_await h.push(true);

}


void test_stream_quote() {
    auto executor = ThreadExecutor::create();
    auto exchange = std::make_shared<Exchange>(ctx, executor);
    auto instrument = exchange->create_instrument("BTCUSD", InstrumentType::margin);
    auto stream = instrument->subscribe<Quote>();    
    Hub<bool> hub;
    executor->run(test_stream_quotes_frag(std::move(stream),hub));
    hub.pop().get();
    
}   

StrategyFragment test_periodic_stream_1(EventStream<PeriodicSnapshot<1> > stream, Hub<bool> &h) {
    PeriodicSnapshot<1> sn1;
    PeriodicSnapshot<1> sn2;
    PeriodicSnapshot<1> sn3;
    bool r = co_await stream.next(sn1);
    CHECK(r);
    auto tm1 = std::chrono::system_clock::now();
    r = co_await stream.next(sn2);
    CHECK(r);
    auto tm2 = std::chrono::system_clock::now();
    r = co_await stream.next(sn3);
    CHECK(r);
    auto tm3 = std::chrono::system_clock::now();

    auto dff12 = std::chrono::duration_cast<std::chrono::milliseconds>(tm2- tm1).count();
    auto dff23 = std::chrono::duration_cast<std::chrono::milliseconds>(tm3- tm2).count();
    CHECK_BETWEEN(950, dff12, 1050);
    CHECK_BETWEEN(950, dff23, 1050);

    CHECK_LESS(sn1.bid,sn1.ask);
    CHECK_GREATER(sn1.bid_size, 0);
    CHECK_GREATER(sn1.ask_size, 0);

    CHECK_BETWEEN(1, sn1.bid , 10000000_dec);
    CHECK_BETWEEN(1, sn1.ask , 10000000_dec);
   
    stream.close();
    r = co_await stream.next(sn1);
    CHECK(!r);
    co_await h.push(true);
}

void test_periodic_stream_1() {
    auto executor = ThreadExecutor::create();
    auto exchange = std::make_shared<Exchange>(ctx, executor);
    auto instrument = exchange->create_instrument("BTCUSD", InstrumentType::margin);
    auto stream = instrument->subscribe<PeriodicSnapshot<1> >();    
    Hub<bool> hub;
    executor->run(test_periodic_stream_1(std::move(stream),hub));
    hub.pop().get();
}

StrategyFragment test_periodic_stream_2(EventStream<PeriodicSnapshot<5> > stream, Hub<bool> &h) {
    PeriodicSnapshot<5> sn1;
    PeriodicSnapshot<5> sn2;
    PeriodicSnapshot<5> sn3;
    bool r = co_await stream.next(sn1);
    CHECK(r);
    auto tm1 = std::chrono::system_clock::now();
    r = co_await stream.next(sn2);
    CHECK(r);
    auto tm2 = std::chrono::system_clock::now();
    r = co_await stream.next(sn3);
    CHECK(r);
    auto tm3 = std::chrono::system_clock::now();

    auto dff12 = std::chrono::duration_cast<std::chrono::milliseconds>(tm2- tm1).count();
    auto dff23 = std::chrono::duration_cast<std::chrono::milliseconds>(tm3- tm2).count();
    CHECK_BETWEEN(4000, dff12, 6000);
    CHECK_BETWEEN(4000, dff23, 6000);

    CHECK_LESS(sn1.bid,sn1.ask);
    CHECK_GREATER(sn1.bid_size, 0);
    CHECK_GREATER(sn1.ask_size, 0);

    CHECK_BETWEEN(1, sn1.bid , 10000000_dec);
    CHECK_BETWEEN(1, sn1.ask , 10000000_dec);
   
    stream.close();
    r = co_await stream.next(sn1);
    CHECK(!r);
    co_await h.push(true);
}

void test_periodic_stream_2() {
    StreamManager::long_interval = 5;
    auto executor = ThreadExecutor::create();
    auto exchange = std::make_shared<Exchange>(ctx, executor);
    auto instrument = exchange->create_instrument("BTCUSD", InstrumentType::margin);
    auto stream = instrument->subscribe<PeriodicSnapshot<5> >();    
    Hub<bool> hub;
    executor->run(test_periodic_stream_2(std::move(stream),hub));
    hub.pop().get();
}

int main() {
    ctx = network::ssl_init_client();
    test_stream_quote();
    test_exchange_object();    
    test_load_currencies();
    test_stream_trades();
    test_periodic_stream_2();
    test_periodic_stream_1();
 
}