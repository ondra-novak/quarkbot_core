#include "config_backtest.hpp"
#include "../common/logger.hpp"
#include "../common/mem_storage.hpp"
#include "quarkbot/common/strategy_config.hpp"
#include "quarkbot/config.hpp"
#include "quarkbot/log.hpp"
#include "quarkbot/config_backend_with_paths.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include "simexec_report_csv.hpp"
#include "quarkbot_compile_config.h"
#ifdef QUARKBOT_LEVELDB
#include "../leveldb/leveldb_storage.hpp"
#endif
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <optional>
#include <stdexcept>
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

    SimulationParams BacktestConfig::configure_simulation_no_report(std::string_view section) {        
        auto sim_config = as_config() / section;
        double slippage = sim_config["slippage"](0.0);
        std::size_t latency = sim_config["latency_ms"](static_cast<std::size_t>(0));

        return {{}, slippage, std::chrono::milliseconds(latency)};
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

    Storage BacktestConfig::configure_storage(std::string_view section) {
        auto stor_config = as_config() / section;
        std::optional<std::filesystem::path> database = stor_config["database"](std::nullopt);
        if (!database.has_value()) return Storage(MemStorage::create(MemStorage::no_history));
#ifdef QUARKBOT_LEVELDB
        std::optional<std::filesystem::path> preset = stor_config["preset"](std::nullopt);
        std::string name = stor_config["name"](std::string("strategy"));

        if (std::filesystem::exists(*database)) {
            if (preset.has_value() && std::filesystem::exists(*preset)
                && std::filesystem::equivalent(*database, *preset)) {
                throw std::runtime_error("Storage: working database is the same as the preset: " + database->string());
            }
            if (!std::filesystem::exists(*database / "CURRENT")) {
                throw std::runtime_error("Storage: refusing to delete, not a LevelDB database: " + database->string());
            }
            std::filesystem::remove_all(*database);
        }
        if (preset.has_value()) {
            if (!std::filesystem::exists(*preset / "CURRENT")) {
                throw std::runtime_error("Storage: preset is not a LevelDB database: " + preset->string());
            }
            std::filesystem::copy(*preset, *database, std::filesystem::copy_options::recursive);
        }
        logInfo("Storage: LevelDB {} (preset: {}, name: {})", database->string(),
                preset.has_value()?preset->string():std::string("none"), name);
        leveldb::Options opts;
        opts.create_if_missing = true;
        return Storage(LevelDBStorageManager::open_db(*database, opts).get_storage(name));
#else
        throw std::runtime_error("Storage: `database` requires LevelDB support (compile with QUARKBOT_LEVELDB=ON)");
#endif
    }

}