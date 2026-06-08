#pragma once

#include "basic_coro/awaitable.hpp"
#include "ifc/config.hpp"
#include "strategy_fragment.hpp"
#include "execution_worker.hpp"
#include "defs.hpp"
#include <functional>
#include <memory>
#include <memory_resource>
#include "tradable_instrument.hpp"
#include "utils/json.hpp"
#include <vector>
namespace quarkbot {

    enum StrategyMode {
        live_trading,
        backtest,
        paper_trading
    };

    class StrategyContext;

    template<typename T>
    concept StrategyClass = requires(T val, StrategyContext ctx) {
        {T(ctx)};
        {val.main()}->std::same_as<StrategyFragment>;
    };



    class StrategyContext {
    public:
        using Config = Config<std::function<std::optional<std::string_view>(std::string_view)> >;

        ///List of tradable instruments available to the strategy
        /** the strategy can query for accounts and exchanges through the instruments */
        std::vector<TradableInstrument> instruments;
        ///Storage associated with the strategy
        PStorage storage;
        ///Reference to strategy execute worker
        ExecutionWorker exec_worker{nullptr};
        ///current strategy mode
        StrategyMode mode;
        ///Strategy configuration
        Config config;
        
        ///co_await on this to wait on stop signal
        /**
        There can be multiple awaiting coroutines. All these coroutines are resumed on stop signal

        @code
            co_await context.stop_signal();     //pause and wakeup on exit
        @endcode
         */
        std::function<awaitable<coro::void_type>()> stop_signal;

        //start strategy instance
        /**
            @param strategy_instance reference to strategy instance - lifetime is handled by caller
             (can be allocated statically)
            @param ctx r-value of context (std::move()) starts the strategy with given context
        */
        
        template<StrategyClass _S>
        friend void start_strategy(_S &strategy_instance, StrategyContext &&ctx) {
            auto worker = ctx.exec_worker;
            worker.run(strategy_instance.start(std::move(ctx)));
        }

        ///create and start the strategy
        /**
        Lifetime is managed by following way 
        - strategy is kept alive until stop signal is activated
        - after this, it schedules self twice to proper cleanup, but then it destroys the strategy        
         */
        template<StrategyClass _S>
        friend StrategyFragment create_and_start_strategy(StrategyContext ctx) {
            //retrieve worker
            auto worker = ctx.exec_worker;
            //retrieve awaitable for stop
            auto stop_awaitable = ctx.stop_signal();
            //create strategy instance
            _S strategy{ctx};
            co_await worker.schedule();
            //run strategy, wait until exit
            co_await strategy.start(std::move(ctx));            
            //wait until context stop
            co_await stop_awaitable;
            //scheduler twice
            co_await worker.schedule();
            co_await worker.schedule();            
            //strategy is destroyed here
        }


    };

}