#pragma once

#include "awaitable_stop.hpp"
#include "basic_coro/pending.hpp"
#include "ifc/config.hpp"
#include "ifc/types.hpp"
#include "strategy_fragment.hpp"
#include "execution_worker.hpp"
#include "defs.hpp"
#include <functional>
#include <memory>
#include <memory_resource>
#include "tradable_instrument.hpp"
#include "utils/json.hpp"
#include <stop_token>
#include <utility>
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
        using Config = Config<std::function<std::optional<std::string_view>(const std::string &)> >;

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

        AwaitableStopToken stop_signal;

        std::shared_ptr<StrategyFragmentGroup> active_group = {};
        
        ///start strategy fragment and add it to fragment group ensuring that strategy will not be destroyed until fragment is finished
        /** Use for long time running fragments, not for short ones. The fragemnt stays registered even if it is already finished */
        void launch(StrategyFragment fragment) {
            active_group->add(std::move(fragment), exec_worker);
        }


        ///create and start the strategy
        /**
        Lifetime is managed by following way 
        - strategy is kept alive until stop signal is activated
        - after this, it schedules self twice to proper cleanup, but then it destroys the strategy        
         */
        template<StrategyClass _S>
        friend StrategyFragment create_and_start_strategy(StrategyContext ctx) {

            ctx.active_group  = std::make_shared<StrategyFragmentGroup>();
            //retrieve worker
            auto worker = ctx.exec_worker;
            //retrieve awaitable for stop
            auto stop_awaitable = ctx.stop_signal();
            //create strategy instance
            _S strategy{ctx};
            co_await worker.schedule();
            //run strategy, wait until exit
            co_await strategy.main();            
            //wait until context stop
            co_await stop_awaitable;
            //join whole group
            co_await ctx.active_group->join();
            //scheduler twice
            co_await worker.schedule();
            co_await worker.schedule();            
            //strategy is destroyed here
        }



    };

}