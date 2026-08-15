#include "config_backtest.hpp"
#include "../common/logger.hpp"
#include "quarkbot/common/strategy_config.hpp"
#include "quarkbot/config.hpp"
#include "quarkbot/config_backend_with_paths.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include "simexec_report_csv.hpp"
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <optional>
#include <string_view>

namespace quarkbot {

    BacktestConfig::Config BacktestConfig::as_config() const {
        return BacktestConfig::Config(ConfigBackendWithPaths(config),'#');
    }


    BacktestConfig BacktestConfig::load(std::filesystem::path ini_config) {
        auto config = load_strategy_config_as_map(ini_config);
        return {std::move(config)};        
    }

    void BacktestConfig::configure_log(std::string_view section) {
        auto log_config = as_config() / section;
        auto log_level = log_config["level"](LogLevel::info);
        std::optional<std::filesystem::path> log_file = log_config["file"](std::nullopt);
        
        if (log_file.has_value()) {
            log_to_file(*log_file);
        } else {
            log_to_stderr();
        }
        log_set_level(log_level);
        //configure logging based on log_level and log_file
    }

    SimulationParams BacktestConfig::configure_simulation(std::string_view section) {        
        auto sim_config = as_config() / section;
        std::filesystem::path report_csv = sim_config["report_file"](std::filesystem::path{"report.csv"});
        double slippage = sim_config["slippage"](0.0);
        std::size_t latency = sim_config["latency_ms"](static_cast<std::size_t>(0));

        auto rpt = open_report(report_csv);
        return {std::move(rpt), slippage, std::chrono::milliseconds(latency)};
    }

    std::vector<WalletInitItem> BacktestConfig::configure_wallet(std::string_view section) {
        std::vector<WalletInitItem> out;
        std::string pfx (section);
        pfx.push_back('#');
        for (auto &[k,v]: config->get_map()) {
            if (k.starts_with(pfx)) {
                std::string_view cur = k;
                cur.remove_prefix(pfx.size())     ;
                Decimal amount = Decimal::from_string(v.first);
                out.emplace_back(std::string(cur), amount);
            }
        }
        return out;
    }

}