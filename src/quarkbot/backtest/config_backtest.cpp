#include "config_backtest.hpp"
#include "../common/logger.hpp"
#include "quarkbot/config.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include "simexec_report_csv.hpp"
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <optional>
#include <string_view>

namespace quarkbot {

    BacktestConfig::Config BacktestConfig::as_config() const {
        return BacktestConfig::Config(ConfigBackend(
            std::shared_ptr<const ConfigMap>(&config,[](auto){})

        ),'#');
    }

    std::optional<std::string_view> BacktestConfig::ConfigMap::operator()(const std::string &key) const {
        auto iter = std::lower_bound(this->begin(), this->end(), std::pair(key, std::string()));
        if (iter == this->end() || iter->first != key) return std::nullopt;
        else return {iter->second};
    }

    BacktestConfig BacktestConfig::load(std::filesystem::path ini_config) {
        std::ifstream f(ini_config);
        if (!f) throw std::runtime_error(std::format("Can't open config file: {}", ini_config.string()));
        std::string content;
        std::copy(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>(), std::back_inserter(content));
        IniReader<ConfigStringReader> ini({content});
        auto kv = ini.create_kv_map('#');
        std::sort(kv.begin(), kv.end());
        return {std::move(kv), std::filesystem::absolute(ini_config).parent_path()};        
    }

    void BacktestConfig::configure_log(std::string_view section) {
        auto log_config = as_config() / section;
        auto log_level = log_config["level"](LogLevel::info);
        std::optional<std::string> log_file = log_config["file"](std::nullopt);
        if (log_file.has_value()) {
            log_to_file(base_path / *log_file);
        } else {
            log_to_stderr();
        }
        log_set_level(log_level);
        //configure logging based on log_level and log_file
    }

    SimulationParams BacktestConfig::configure_simulation(std::string_view section) {
        auto sim_config = as_config() / section;
        std::string_view report_csv = sim_config["report_file"]("report.csv");
        double slippage = sim_config["slippage"](0.0);
        std::size_t latency = sim_config["latency_ms"](static_cast<std::size_t>(0));

        auto rpt = open_report(base_path/report_csv);
        return {std::move(rpt), slippage, std::chrono::milliseconds(latency)};
    }

    std::vector<WalletInitItem> BacktestConfig::configure_wallet(std::string_view section) {
        std::vector<WalletInitItem> out;
        std::string pfx (section);
        pfx.push_back('#');
        auto iter = std::lower_bound(config.begin(), config.end(), std::pair(pfx, std::string()));
        while (iter != config.end()) {
            if (!iter->first.starts_with(pfx)) break;
            std::string_view cur = iter->first;
            cur.remove_prefix(pfx.size());
            Decimal amount = Decimal::from_string(iter->second);
            out.emplace_back(std::string(cur), amount);
            ++iter;
        }
        return out;
    }

}