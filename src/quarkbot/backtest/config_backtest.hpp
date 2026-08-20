#pragma once

#include "backtest.hpp"
#include "quarkbot/config.hpp"
#include "quarkbot/config_backend_with_paths.hpp"
#include "quarkbot/types.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>

namespace quarkbot {

///manages config file for backtest environment
struct BacktestConfig {

    using Config = ::quarkbot::ConfigT<ConfigBackendWithPaths>;

    ///configuration map (key value)
    std::shared_ptr<ConfigBackendWithPathsMap> config;

    ///load configuration from a file
    static BacktestConfig load(std::filesystem::path ini_config);

    ///return config as Config object
    /**
        @return Config object
        @note Object holds A REFERENCE to the actual map, so you still need to keep original object alive.
    */
    Config as_config() const;

    ///configure logger 
    /**
        Reads level and file, parses it and sets logger to proper logging
        This has global effect
        @param section which section contains logger setup
    */
    void configure_log(std::string_view section = "log") ;

    ///configure simulation parameters
    /**
        @param section which section contains simulation parameters
        @return simulation parameters

    @code
    report_file=report.csv
    slippage = 0.01
    latency_ms = 100
    @endcode
    */
    SimulationParams configure_simulation(std::string_view section = "simulation");
    SimulationParams configure_simulation_no_report(std::string_view section = "simulation");
    ///configure wallet
    /**
        @param section which section contains wallet

    @code
    USD=12345.67
    BTC=10.000
    DOGE=3333
    @endcode

    @note by default, currency which is not initialized is considered as infinite, disabling balance check and liquidation orders. 
        It still counts balance, so result can be a negative balance
     */
    std::vector<WalletInitItem> configure_wallet(std::string_view section = "wallet");

};







}