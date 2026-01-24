#pragma once

#include "defs.hpp"
#include <vector>
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
        ///Reference to strategy execute worker
        PExecutionWorker exec_worker;
        StrategyMode mode;
    };

}