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
    ///function called to initialize debugger - (debugger runs in different thread)
    /**
    @param #1 stop token, debugger should exit when token is signaled
    @param #2 shared pointer to debugger interface allowing to determine state of backtest execution and perform steps or run
     */
    std::function<void(std::stop_token, std::shared_ptr<IBacktestDebugger>)> debugger = {};
    ///function is called to initialize inveronment, for example scripting language context
    /**
    Function receives configuration object
     */
    std::function<void(const BacktestConfig::Config &)> init_env = {};
};

///start backtest - entry point
int backtest_entry_point(BacktestStartParams params);


}