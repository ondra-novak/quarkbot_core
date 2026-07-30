#pragma once

#include "context.hpp"
#include "quarkbot/strategy_fragment.hpp"
namespace quarkbot {

    ///create and start the strategy
    /**
        @param ctx context object 
        @param args arguments for main()
        @return StrategyFragment prepared to be run.
    */
    template<typename _S, std::derived_from<StrategyContext> _Context, typename ... Args>
    requires(StrategyClass<_S, _Context, Args ...>)
    inline StrategyFragment create_and_start_strategy(_Context ctx, Args  ... args) {

        ctx.active_group  = std::make_shared<StrategyFragmentGroup>();
        //retrieve worker
        auto worker = ctx.exec_worker;
        //retrieve awaitable for stop
        auto stop_awaitable = ctx.stop_signal();
        //retrieve active group before ctx is moved into the strategy - it must
        //keep the fragment group alive even after the strategy itself is destroyed
        auto active_group = ctx.active_group;
        //create strategy instance
        _S strategy{std::move(ctx)};
        //wait to start worker (in case of backtest executor)
        co_await worker.schedule();
        //run strategy, wait until exit
        co_await strategy.main(std::forward<Args>(args)...);
        //wait until context stop
        co_await stop_awaitable;
        //join whole group
        co_await active_group->join();
        //scheduler twice
        co_await worker.schedule();
        co_await worker.schedule();            
        //strategy is destroyed here
    }


    ///main function to start strategy
    /**
    @tparam _S strategy
    @tparam Args additional arguments        
    @param argc argc
    @param argv argv
    @param strategy_args optional arguments passed to the strategy main
    @return main's return
    */

    template<typename _Strategy,  typename ... Args>
    requires(StrategyClass<_Strategy, StrategyContext, Args ...>)
    int strategy_main(int argc, char **argv, Args ...  strategy_args) {
        return entry_point(argc, argv, [strategy_args...](StrategyContext &&context, StrategyContext::Config &&){
            return create_and_start_strategy<_Strategy>(std::move(context), std::move(strategy_args)...);
        });
    }

    ///Entry point - must be implemented by backtest or live library, not implemented in quarkbot core
    /**
        @param argc argc
        @param argv argv
        @param start_fn strategy startup function - recommended to call create_and_start_strategy - the function receives context and
            environment configuration file - backtest or live config. 
    */
    int entry_point(int argc, char **argv, std::function<StrategyFragment(StrategyContext &&, const StrategyContext::Config &)> start_fn);


}