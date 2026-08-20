#pragma once

#include "quarkbot/backtest/config_backtest.hpp"
#include "quarkbot/backtest/ibacktest_debugger.hpp"
#include <filesystem>
#include <functional>
#include <stop_token>
namespace quarkbot {

struct BacktestStartParams {
    ///strategy configuration
    std::filesystem::path strategy_config;
    ///backtest configuration
    std::filesystem::path backtest_config;
    ///strategy factory
    std::function<StrategyFragment(StrategyContext &&)> start_fn;
    ///function called to initialize debugger 
    /**
    @param #1 shared pointer to debugger interface allowing to determine state of backtest execution and perform steps or run
    @param #2 storage object to report variables

    @note The caller exepcts that function creates own thread. Function closure is destroyed at the end of the backest, which can be used
    to detect end of debugging session
     */
    std::function<void(std::shared_ptr<IBacktestDebugger>, Storage)> debugger = {};
    ///function is called to initialize inveronment, for example scripting language context
    /**
    Function receives configuration object
     */
    std::function<void(const BacktestConfig::Config &)> init_env = {};

    bool json_report = false;

    
};

///start backtest - entry point
int backtest_entry_point(BacktestStartParams params);


}