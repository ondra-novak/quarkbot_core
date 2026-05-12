#include "check.h"

#include "exchanges/bitfinex/instrument_map.hpp"
#include "exchanges/bitfinex/exchange.hpp"
#include "ifc/context.hpp"
#include "ifc/hub.hpp"
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

network::PSSL_CTX ctx;

InstrumentMap create_map_object() {
    auto client = network::SecureRestClient{ctx,"https://api-pub.bitfinex.com/v2"};
    client.add_header("User-Agent", "quarkbot/1.0 tests");
    return InstrumentMap(std::move(client));
    
    
}

void test_load_currencies() {
    auto map = create_map_object();
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


int main() {
    ctx = network::ssl_init_client();
    test_stream_quote();
    test_exchange_object();    
    test_load_currencies();
    test_stream_trades();
 
}