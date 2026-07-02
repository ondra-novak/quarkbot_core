#pragma once


#include "quarkbot/context.hpp"
#include "quarkbot/exchange.hpp"
#include "quarkbot/stream/auction.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <variant>
namespace quarkbot {

using CustomBacktestEvent = std::function<void()>;

struct BacktestEvent {
    std::string symbol;
    std::chrono::system_clock::time_point time;
    std::variant<CustomBacktestEvent,Trade,Quote,Auction> data;
};

using BacktestDataSource = std::function<bool(BacktestEvent &ev)>;


using WalletInitItem = std::pair<std::string, Decimal>;

using ReportSink = std::function<void(const POrderAData &, const OrderInternalData::Update &)>;

struct SimulationParams {
    std::variant<std::monostate, std::filesystem::path, ReportSink> reporter = {};
    double slippage = {};
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
        context.template create_and_start_strategy<_Strategy>(std::move(context));        
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

protected:
    ExecutionWorker _worker;
    Exchange _exchange;
    Account _account;
    std::stop_source stop_src;

    void init_context_basic(std::span<const std::string_view> instruments, StrategyContext &ctx);
    void init_context_basic( StrategyContext &ctx);

};
}