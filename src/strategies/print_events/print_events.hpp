#pragma once
#include "quarkbot/context.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/market_instrument.hpp"
#include "quarkbot/stream/closedbar.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"
#include <iostream>
#include <stop_token>

using namespace quarkbot;

class PrintEventStrategy {
public:

    StrategyContext context;

    PrintEventStrategy(StrategyContext context)
            :context(std::move(context)) {}

    StrategyFragment main([[maybe_unused]]std::string_view example_argument) {
        std::vector<std::function<void()>> cleanup_actions;

        for (MarketInstrument instr: this->context.instruments) {
            print_quotes(instr, instr.subscribe<Quote>().stop_on(context.stop_signal));
            print_trades(instr, instr.subscribe<Trade>().stop_on(context.stop_signal));
            print_bars(instr, instr.subscribe<ClosedBarInterval<300>>().stop_on(context.stop_signal));
        }
        co_return;
    }


    StrategyFragment print_quotes(MarketInstrument instrument, EventStream<Quote> s) {
        Quote q;
        while(co_await s.receive(q)) {
            std::cout << q.time << " " <<  instrument.get_info().name << " bid:" << q.bid.to_double() << " ask:" << q.ask.to_double() << std::endl;
        }
        std::cout << "Quote stream closed: " << instrument.get_info().name << std::endl;
    }

    StrategyFragment print_trades(MarketInstrument instrument, EventStream<Trade> s) {
        Trade t;
        while(co_await s.receive(t)) {
            std::cout << t.time << " " << instrument.get_info().name << " trade:" << t.price.to_double() << std::endl;
        }
        std::cout << "Trade stream closed: " << instrument.get_info().name << std::endl;
    }

    StrategyFragment print_bars(MarketInstrument instrument, EventStream<ClosedBarInterval<300>> s) {
        ClosedBarInterval<300> cb;
        while(co_await s.receive(cb)) {
            std::cout << cb.start_time << " " << instrument.get_info().name << " ohlc:" 
                << cb.open.to_double() << ","
                << cb.high.to_double() << ","
                << cb.low.to_double() << ","
                << cb.close.to_double()
                << std::endl;
        }
        std::cout << "Bar stream closed: " << instrument.get_info().name << std::endl;

    }
};
