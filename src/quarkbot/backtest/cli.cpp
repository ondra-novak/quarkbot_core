#include "cli.hpp"
#include "backtest.hpp"
#include "config_backtest.hpp"
#include "config_datasource.hpp"
#include "config_instrument.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/log.hpp"
#include "../common/mem_storage.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>


namespace quarkbot {

StrategyContext::Config loadStrategyConfig(std::filesystem::path cfg)  {
    std::fstream f(cfg);
    if (!f) throw std::runtime_error(std::format("Failed to open: {}", cfg.string()));
    IniReaderFromStream ini(f);
    auto kv = ini.create_kv_map();
    return {[map = std::make_shared<std::unordered_map<std::string, std::string>>(kv.begin(), kv.end())](const std::string &key) -> std::optional<std::string_view> {
        auto iter = map->find(key);
        if (iter == map->end()) return std::nullopt;
        return iter->second;
    },'#'};
    
}


int start_indirect(int argc, char **argv, StrategyFragment (*startup_fn)(StrategyContext &&)) {

    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <strategy_config.ini> <backtest_config.ini>\n", argv[0]);
        return 1;
    }

    std::filesystem::path strategy_config_path = argv[1];
    std::filesystem::path backtest_config_path = argv[2];

    BacktestConfig cfg;

    try {
        cfg = BacktestConfig::load(backtest_config_path);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "Failed to initialize backtest config %s, exception %s\n", backtest_config_path.c_str(), e.what());
        return 2;
    }

    try {

        BacktestEnv bt("backtest-account", cfg.configure_wallet(), configure_instruments(backtest_config_path), cfg.configure_simulation());
        auto data_source = configure_datasources(backtest_config_path);

        StrategyContext ctx;
        ctx.storage = Storage(std::make_shared<MemStorage>());
        ctx.config = loadStrategyConfig(strategy_config_path);;        
        bt.add_strategy(startup_fn, std::move(ctx));
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