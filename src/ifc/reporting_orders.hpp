#pragma once
#include "reporting.hpp"
#include <stdexcept>


namespace quarkbot {

    inline void IReporter::attach(PReporter reporter, PTradableInstrument instrument, std::stop_token stp, PExecutionWorker worker) {

        if (!worker) worker = worker->current();
        if (!worker) throw std::runtime_error("Requires execution worker");
        
        auto order_report = [](PReporter reporter, PTradableInstrument instrument, std::stop_token stp)->StrategyFragment {
            auto stream = instrument->subscribe<OrderEvent>();
            std::stop_callback _(stp, [&]{stream.close();});
            OrderEvent ev;
            while (co_await stream.next(ev)) {
                reporter->report_order(ev.order.value(), ev.update);
            }
        };
        auto trade_report = [](PReporter reporter, PMarketInstrument instrument, std::stop_token stp)->StrategyFragment {
            auto stream = instrument->subscribe<Trade>();
            std::stop_callback _(stp, [&]{stream.close();});
            Trade ev;
            while (co_await stream.next(ev)) {
                reporter->report_trade(instrument, ev);
            }
        };
        auto quote_report = [](PReporter reporter, PMarketInstrument instrument, std::stop_token stp)->StrategyFragment {
            auto stream = instrument->subscribe<Quote>();
            std::stop_callback _(stp, [&]{stream.close();});
            Quote ev;
            while (co_await stream.next(ev)) {
                reporter->report_quote(instrument, ev);
            }
        };
        worker->run(order_report(reporter, instrument, stp));
        worker->run(trade_report(reporter, instrument->get_instrument(), stp));
        worker->run(quote_report(reporter, instrument->get_instrument(), stp));
}

}