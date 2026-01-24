#pragma once

#include "defs.hpp"
#include "utils/dispatcher.hpp"
namespace quarkbot {

    enum StrategyMode {
        live_trading,
        backtest,
        paper_trading
    };

    class StrategyContext {
    public:
        ///List of tradable instruments available to the strategy
        /** the strategy can query for accounts and exchanges through the instruments */
        std::vector<PTradableInstrument> instruments;
        ///Scheduler associated with the strategy
        /** Note this can be simulated scheduler in case that backest is running */
        PScheduler scheduler;
        ///Storage associated with the strategy
        PStorage storage;
        ///Weak reference to strategy dispatcher, used to post events to strategy's thread
        /** Services must be called from strategy's thread, if you need to call services
        from diffrent thread, you need to use the dispatcher 
            Don't store strong reference to dispatcher. Once the dispatcher is destroyed,
            the strategy is probably stopped and you would unable to post any events anyway. 
            The null weak pointer can be used to determine that strategy is stopped.
        */
        std::weak_ptr<Dispatcher> dispatcher;
        StrategyMode mode;
    };

}