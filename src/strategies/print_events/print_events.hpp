#pragma once
#include "ifc/context.hpp"
#include "ifc/stream/quote.hpp"
#include "ifc/stream/trade.hpp"
#include "ifc/stream/closedbar.hpp"
#include "ifc/event_stream.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/tradable_instrument.hpp"
#include <iostream>
#include <stop_token>

using namespace quarkbot;

static_assert(StreamWithConstantParam<ClosedBarInterval<300> > );
class PrintEventStrategy {
public:

    StrategyContext context;

    PrintEventStrategy(StrategyContext context)
            :context(std::move(context)) {}

    StrategyFragment main() {
        std::stop_source stop_sig;
        std::vector<std::function<void()>> cleanup_actions;

        for (auto &x: this->context.instruments) {
            auto instr = x.get_instrument();
            print_quotes(instr, instr.subscribe<Quote>().stop_on(stop_sig));
            print_trades(instr, instr.subscribe<Trade>().stop_on(stop_sig));
            print_bars(instr, instr.subscribe<ClosedBarInterval<300>>().stop_on(stop_sig));
        }
        co_await this->context.stop_signal();
        stop_sig.request_stop();
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
            std::cout << cb.interval_begin() << " " << instrument.get_info().name << " ohlc:" 
                << cb.open.to_double() << ","
                << cb.high.to_double() << ","
                << cb.low.to_double() << ","
                << cb.close.to_double()
                << std::endl;
        }
        std::cout << "Bar stream closed: " << instrument.get_info().name << std::endl;

    }
};
