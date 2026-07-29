#pragma once

#include "backtest.hpp"
#include "quarkbot/config.hpp"
#include "quarkbot/types.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>

namespace quarkbot {

///manages config file for backtest environment
struct BacktestConfig {

    class ConfigMap final : public std::vector<std::pair<std::string, std::string> >, public IConfigBackend {;
    public:
        using Super = std::vector<std::pair<std::string, std::string> >;
        using Super::Super;
        ConfigMap(Super &&other):Super(std::move(other)) {}
        virtual std::optional<std::string_view> operator()(const std::string &key) const override;
    };

    using Config = ::quarkbot::ConfigT<ConfigBackend>;

    ///configuration map (key value)
    ConfigMap config;
    ///base path
    std::filesystem::path base_path;

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