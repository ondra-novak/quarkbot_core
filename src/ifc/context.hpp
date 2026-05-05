#pragma once

#include "basic_coro/coroutine.hpp"
#include "execution_worker.hpp"
#include "defs.hpp"
#include "memory.hpp"
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


    class StrategyFragment : public coro::coroutine<void> {
    public:
        class promise_type: public coro::coroutine<void>::promise_type {
        public:            
            void *operator new(std::size_t sz) {return mem_pool.allocate(sz);}
            void operator delete(void *ptr, std::size_t sz) {return mem_pool.deallocate(ptr, sz);}
        };

        StrategyFragment() = default;
        StrategyFragment(coro::coroutine<void> x):coro::coroutine<void>(std::move(x)) {}
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