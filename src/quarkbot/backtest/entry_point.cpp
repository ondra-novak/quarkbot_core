
#include "quarkbot/backtest/config_backtest.hpp"
#include "quarkbot/backtest/config_datasource.hpp"
#include "quarkbot/backtest/config_instrument.hpp"
#include "quarkbot/common/strategy_config.hpp"
#include "quarkbot/config.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include "quarkbot/common/mem_storage.hpp"
#include "quarkbot/strategy_main.hpp"
#include <fstream>
#include <iostream>
#include <ostream>

namespace quarkbot {

    int entry_point(std::string_view program_name, std::span<const char *const > args, std::function<StrategyFragment(StrategyContext &&, const StrategyContext::Config &)> start_fn){
        //TODO - argv[1] = strategy config, argv[2] = backtest config + python config

        if (args.size() != 2) {
            std::print(std::cerr, "Usage: {} <strategy_config.ini> <backtest_config.ini>\n", program_name);
            return 1;
        }

    std::filesystem::path strategy_config_path = args[0];
    std::filesystem::path backtest_config_path = args[1];

    BacktestConfig cfg;

    try {
        cfg = BacktestConfig::load(backtest_config_path);
        cfg.configure_log();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Failed to initialize backtest config %s, exception %s\n", backtest_config_path.c_str(), e.what());
        return 2;
    }

    try {

        BacktestEnv bt("backtest-account", cfg.configure_wallet(), configure_instruments(backtest_config_path), cfg.configure_simulation());
        auto data_source = configure_datasources(backtest_config_path);

        StrategyContext ctx;
        ctx.storage = MemStorage::create(MemStorage::no_history);        
        ctx.config = load_strategy_config(strategy_config_path);
        bt.add_strategy([start_fn = std::move(start_fn), config = cfg.as_config()](StrategyContext &&context){
            return start_fn(std::move(context), config);
        },  std::move(ctx));
        logInfo("Backtest started");
        bt.run(std::move(data_source));
        logInfo("Backtest ended");

    } catch (const std::exception &e) {
        logFatal("Exception: {}",e.what());
        return 3;
    }

    return 0;
    

    }

}