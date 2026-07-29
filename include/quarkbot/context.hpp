#pragma once

#include "awaitable_stop.hpp"
#include "config.hpp"
#include "quarkbot/message_bus.hpp"
#include "quarkbot/storage.hpp"
#include "strategy_fragment.hpp"
#include "execution_worker.hpp"
#include "defs.hpp"
#include <concepts>
#include <functional>
#include <memory>
#include "tradable_instrument.hpp"
#include <utility>
#include <vector>
namespace quarkbot {

    enum StrategyMode {
        live_trading,
        backtest,
        paper_trading
    };

    class StrategyContext;

    template<typename T, typename Context, typename ... Args>
    concept StrategyClass = requires(T val, Context ctx, Args &&... args) {
        {T(std::move(ctx))};
        {val.main(std::forward<Args>(args)...)}->std::same_as<StrategyFragment>;
    };


    class StrategyContext {
    public:
        using Config = ::quarkbot::Config<std::function<std::optional<std::string_view>(const std::string &)> >;

        ///List of tradable instruments available to the strategy
        /** the strategy can query for accounts and exchanges through the instruments */
        std::vector<TradableInstrument> instruments ={};
        ///Storage associated with the strategy
        Storage storage = {};
        ///Message bus allows to strategy to communicate with outside environment
        MessageBus msg_bus = {};
        ///Reference to strategy execute worker
        ExecutionWorker exec_worker{nullptr};
        ///current strategy mode
        StrategyMode mode;
        ///Strategy configuration
        Config config;

        AwaitableStopToken stop_signal;

        std::shared_ptr<StrategyFragmentGroup> active_group = {};
        
        ///start strategy fragment and add it to fragment group ensuring that strategy will not be destroyed until fragment is finished        
        void run(StrategyFragment fragment) {
            active_group->add(std::move(fragment), exec_worker);
        }


      



    };

}