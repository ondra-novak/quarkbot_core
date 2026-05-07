#include "print_events.hpp"
#include "ifc/defs.hpp"
#include "ifc/streaming.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/tradable_instrument.hpp"
#include <iostream>
#include <stop_token>

using namespace quarkbot;


StrategyFragment print_quotes(PMarketInstrument instrument, EventStream<Quote> s, std::stop_token tkn) {
    std::stop_callback __(tkn,[&]{
        s.close();
    });
    Quote q;
    while(co_await s.next(q)) {
        std::cout << q.time << " " <<  instrument->get_info().name << " bid:" << q.bid.to_double() << " ask:" << q.ask.to_double() << std::endl;
    }
    std::cout << "Quote stream closed: " << instrument->get_info().name << std::endl;
}

StrategyFragment print_trades(PMarketInstrument instrument, EventStream<Trade> s, std::stop_token tkn) {
    std::stop_callback __(tkn,[&]{
        s.close();
    });
    Trade t;
    while(co_await s.next(t)) {
        std::cout << t.time << " " << instrument->get_info().name << " trade:" << t.price.to_double() << std::endl;
    }
    std::cout << "Trade stream closed: " << instrument->get_info().name << std::endl;
}

StrategyFragment print_bars(PMarketInstrument instrument, EventStream<ClosedBarInterval<300>> s, std::stop_token tkn) {
    std::stop_callback __(tkn,[&]{
        s.close();
    });
    ClosedBarInterval<300> cb;
    while(co_await s.next(cb)) {
        std::cout << cb.interval_begin() << " " << instrument->get_info().name << " ohlc:" 
            << cb.open.to_double() << ","
            << cb.high.to_double() << ","
            << cb.low.to_double() << ","
            << cb.close.to_double()
            << std::endl;
    }
    std::cout << "Bar stream closed: " << instrument->get_info().name << std::endl;

}

StrategyFragment  print_events(StrategyContext &context) {

    std::stop_source src;

    for (auto &x: context.instruments) {
        auto instr = x->get_instrument();
        print_quotes(instr, instr->subscribe<Quote>(), src.get_token());
        print_trades(instr, instr->subscribe<Trade>(), src.get_token());
        print_bars(instr, instr->subscribe<ClosedBarInterval<300>>(), src.get_token());
    }

    co_await context.stop_signal();

    src.request_stop();

    co_return;

}