#pragma once

#include "strategy_fragment.hpp"
#include "execution_worker.hpp"
#include "defs.hpp"
#include <functional>
#include <memory>
#include <memory_resource>
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
        ///Storage associated with the strategy
        PStorage storage;
        ///Reference to strategy execute worker
        PExecutionWorker exec_worker;
        ///current strategy mode
        StrategyMode mode;
        ///Reporter - strategy should report it state by this object
        PReporter reporter;

        ///co_await on this to wait on stop signal
        /**
        There can be multiple awaiting coroutines. All these coroutines are resumed on stop signal
         */
        std::function<awaitable<coro::void_type>()> stop_signal;

    };

}