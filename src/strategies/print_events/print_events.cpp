#include "print_events.hpp"
#include "ifc/defs.hpp"
#include "ifc/market_events.hpp"
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

StrategyFragment  print_events(StrategyContext &context) {

    std::stop_source src;

    for (auto &x: context.instruments) {
        auto instr = x->get_instrument();
        print_quotes(instr, instr->subscribe<Quote>(), src.get_token());
        print_trades(instr, instr->subscribe<Trade>(), src.get_token());
    }

    co_await context.stop_signal();

    src.request_stop();

    co_return;

}