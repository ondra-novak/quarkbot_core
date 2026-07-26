#pragma once

#include "context.hpp"
namespace quarkbot {

    ///create and start the strategy
    template<typename _S, std::derived_from<StrategyContext> _Context>
    requires(StrategyClass<_S, _Context>)
    inline StrategyFragment create_and_start_strategy(_Context ctx) {

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
        co_await worker.schedule();
        //run strategy, wait until exit
        co_await strategy.main();
        //wait until context stop
        co_await stop_awaitable;
        //join whole group
        co_await active_group->join();
        //scheduler twice
        co_await worker.schedule();
        co_await worker.schedule();            
        //strategy is destroyed here
    }
}