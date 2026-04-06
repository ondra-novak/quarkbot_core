#pragma once

#include "basic_coro/coroutine.hpp"
#include "execution_worker.hpp"
#include "defs.hpp"
#include "memory.hpp"
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



        ///Start strategy fragment
        /**
            Use this function to start strategy - initial fragment
            You can start strategy fragments by calling them directly if they are called in context of other fragment
            This function is useful to start fragmen from different thread
         */
        void start(StrategyFragment s) {exec_worker->run(std::move(s));}

        ///Define startegy fragment or awaitable function which is executed when strategy is stopped
        /**
        
        @code 
        StrategyFragment stop_function() {
            //...
        }
        context.on_stop(stop_function());
        @endcode

        The strategy is considered stopped when this function finishes. 

        @note there can be only one stop function
        @note if the stop function doesn't exits, there is no way to stop strategy at all
        @note during stop execution, streams can be already stopped, but it should be possible to cancel orders
        */

        void on_stop(awaitable<void> s) {on_stop_awaitable = std::move(s);}
    protected:
        awaitable<void> on_stop_awaitable;

    };

}