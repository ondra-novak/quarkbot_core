#pragma once

#include "basic_coro/awaitable.hpp"
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

    

    class AwaitableStopToken: public std::stop_token {
    public:


        template<typename _type>
        struct Impl {
            coro::awaitable_result<_type> res;
            ExecutionWorker worker;
            void operator()() {
                //move worker because res() will destroy this
                auto wrk = std::move(worker);
                if (wrk) wrk.resume(res());
                else res();
            }
        };

        template<typename _type>
        class State : public std::variant<std::stop_token, std::stop_callback<Impl<_type> > >{
        public:
            using std::variant<std::stop_token, std::stop_callback<Impl<_type> > >::variant;
            State(State &&other): std::variant<std::stop_token, std::stop_callback<Impl<_type> > >(std::move(std::get<std::stop_token>(other))) {}
        };

        using nothing = std::array<char, sizeof(State<void>)>;


        AwaitableStopToken() = default;
        AwaitableStopToken(std::stop_token tkn):std::stop_token(std::move(tkn)) {}        

        awaitable<nothing> operator ()() const;
        awaitable<nothing> operator co_await() const;
    };



    inline awaitable<AwaitableStopToken::nothing> AwaitableStopToken::operator ()() const {
            if (this->stop_requested()) return {nothing{}};
            return [st = State<nothing>(std::in_place_type<std::stop_token>,*this)](auto promise) mutable  {
                auto tkn =std::move( std::get<std::stop_token>(st));
                st.emplace<std::stop_callback<Impl<nothing> > >(tkn, Impl{std::move(promise), ExecutionWorker::current()});
            };
        }   
    inline awaitable<AwaitableStopToken::nothing> AwaitableStopToken::operator co_await() const {
            return this->operator()();
    }

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
            co_await strategy.main();            
            //wait until context stop
            co_await stop_awaitable;
            //scheduler twice
            co_await worker.schedule();
            co_await worker.schedule();            
            //strategy is destroyed here
        }



    };

}