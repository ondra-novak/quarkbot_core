#include "print_events.hpp"
#include "ifc/context.hpp"
#include "ifc/stream/quote.hpp"
#include "ifc/stream/trade.hpp"
#include "ifc/stream/closedbar.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/tradable_instrument.hpp"
#include <iostream>
#include <stop_token>

using namespace quarkbot;

class PrintEventStrategy {
public:

    StrategyContext context;

    StrategyFragment start(StrategyContext context) {
        this->context = std::move(context);
        std::stop_source src;

        for (auto &x: this->context.instruments) {
            auto instr = x.get_instrument();
            print_quotes(instr, instr.subscribe<Quote>(), src.get_token());
            print_trades(instr, instr.subscribe<Trade>(), src.get_token());
            print_bars(instr, instr.subscribe<ClosedBarInterval<300>>(), src.get_token());
        }
        co_await this->context.stop_signal();
        src.request_stop();
        co_return;
    }


    StrategyFragment print_quotes(MarketInstrument instrument, EventStream<Quote> s, std::stop_token tkn) {
        std::stop_callback __(tkn,[&]{
            s.close();
        });
        Quote q;
        while(co_await s.receive(q)) {
            std::cout << q.time << " " <<  instrument.get_info().name << " bid:" << q.bid.to_double() << " ask:" << q.ask.to_double() << std::endl;
        }
        std::cout << "Quote stream closed: " << instrument.get_info().name << std::endl;
    }

    StrategyFragment print_trades(MarketInstrument instrument, EventStream<Trade> s, std::stop_token tkn) {
        std::stop_callback __(tkn,[&]{
            s.close();
        });
        Trade t;
        while(co_await s.receive(t)) {
            std::cout << t.time << " " << instrument.get_info().name << " trade:" << t.price.to_double() << std::endl;
        }
        std::cout << "Trade stream closed: " << instrument.get_info().name << std::endl;
    }

    StrategyFragment print_bars(MarketInstrument instrument, EventStream<ClosedBarInterval<300>> s, std::stop_token tkn) {
        std::stop_callback __(tkn,[&]{
            s.close();
        });
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

quarkbot::StrategyFragment print_events(StrategyContext &&context) {
    return create_and_start_strategy<PrintEventStrategy>(std::move(context));
}