#pragma once
#include "basic_coro/sync_await.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/exchange.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include <chrono>
#include <quarkbot/abstract/backtest_data_source.hpp>
#include <filesystem>
#include <variant>
namespace quarkbot {

struct SimulationParams {
    ///Report sink is a function that is called for each order update, it can be used to generate reports
    //  - or you can specify a path to a file where the report will be written in CSV format.
    //  Default is no report sink, no report will be generated.
    std::variant<std::monostate, std::filesystem::path, ReportSink> reporter = {};
    ///Slippage in percent, default is 0.0 (no slippage), for example 0.001 means 0.1% slippage, 0.01 means 1% slippage
    double slippage = {};
    ///How long the order will take to be executed, default is 0 (no latency), for example 100ms means that the order will be executed after 100ms
    /** simulates how long the order travels through wires to the exchange and back. It is total round-trip time */
    std::chrono::system_clock::duration latency = {};    
};

///Backtest environment 
class BacktestEnv {
public:

    ///construct backtest environment
    /**
    @param account_name name of the account to be created on the simulated exchange
    @param wallet initial wallet state - list of currency and their amounts
    @param instruments list of instruments to be added to the simulated exchange
    @param sim_params simulation parameters - slippage, latency, report sink
    */ 
    BacktestEnv(std::string_view account_name,
            std::span<const WalletInitItem> wallet,
            std::span<const InstrumentDescription> instruments,
            const SimulationParams &sim_params
        );

    ///add strategy to the backtest environment and run it
    /**
    @tparam _Strategy strategy class to be added
    @tparam _Context strategy context type, default is StrategyContext
    @param instruments list of instrument names to be added to the strategy context (as a tradable instrument)
    @param context strategy context to be passed to the strategy constructor, default is default constructed StrategyContext. 
        If you need initailize other members such a config, storage, etc, you can pass your own context object.
         The function will add tradable instruments to the context and then pass it to the strategy constructor.
    @note The strategy class must have a constructor that takes a StrategyContext (or derived)

    @note Thet strategy is not run immediately, it is scheduled to run on the backtest executor. The backtest executor is run by the run() function of the BacktestEnv class.
    */
    template<typename _Strategy, typename _Context = StrategyContext>
    requires(StrategyClass<_Strategy, _Context>)
    void add_strategy(std::span<const std::string_view> instruments , _Context &&context = _Context{}) {
        init_context_basic(instruments, context);
        context.template create_and_start_strategy<_Strategy>(std::move(context));        
    }

    ///add strategy to the backtest environment and run it
    /**
    @tparam _Strategy strategy class to be added
    @tparam _Context strategy context type, default is StrategyContext
    @param context strategy context to be passed to the strategy constructor, default is default constructed StrategyContext. 
        If you need initailize other members such a config, storage, etc, you can pass your own context object.
        The function adds all instruments from the backtest environment to the context and then pass it to the strategy constructor.
    @note The strategy class must have a constructor that takes a StrategyContext (or derived)

    @note Thet strategy is not run immediately, it is scheduled to run on the backtest executor. The backtest executor is run by the run() function of the BacktestEnv class.
    */

    template<typename _Strategy, typename _Context = StrategyContext>
    requires(StrategyClass<_Strategy, _Context>)
    void add_strategy(_Context &&context = _Context{}) {
        init_context_basic( context);
        _strategy_group.add(context.template create_and_start_strategy<_Strategy>(std::move(context)), _worker);
    }


    ///run backtest with given data source
    /**
    @param data_source function that provides backtest events. It is called repeatedly until it returns false. 
                The function is called with a reference to a BacktestEvent object that is filled with the next event.
                The function should return true if there is a next event, false if there are no more events. 
                The function should fill the BacktestEvent object with the next event. 
                The event can be a trade, quote, auction or a custom event. 
                The event is processed by the backtest executor and the simulated exchange.
                The event is processed in the order it is provided by the data source

    @retval true if the backtest was run successfully
    @retval false there were no events to process (data source returned false on first call)

    @note Events must be ordered by time, otherwise the backtest will not be correct.
    */
    bool run(BacktestDataSource data_soruce);

    auto get_execution_worker() const {return _worker;}
    auto get_exchange() const {return _exchange;}
    auto get_account() const {return _account;}
    auto get_stop_token() const {return stop_src.get_token();}
    
    void stop() {
        stop_src.request_stop();
    }

    ~BacktestEnv() {
        stop();
        join();
    }

    void join() {
        auto pending = _strategy_group.join().launch();
        while (!pending.await_ready()) {
            _worker.join();
        }
        coro::sync_await(pending);
    }

protected:
    ExecutionWorker _worker;
    Exchange _exchange;
    Account _account;
    std::stop_source stop_src;
    StrategyFragmentGroup _strategy_group;


    void init_context_basic(std::span<const std::string_view> instruments, StrategyContext &ctx);
    void init_context_basic( StrategyContext &ctx);

};
}