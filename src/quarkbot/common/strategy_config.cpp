#include "strategy_config.hpp"
#include "quarkbot/config.hpp"
#include "quarkbot/config_backend_with_paths.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace quarkbot {


    static void load_strategy_config_map(const std::filesystem::path &path, 
                    ConfigBackendWithPathsMap &mp, std::unordered_set<std::filesystem::path> &processed) {
    
        if (!processed.insert(path).second) return;
        std::ifstream f(path);
        auto base_path = path.parent_path();
        if (!f) throw std::runtime_error(std::format("Failed to open: {}", path.string()));
        IniReaderFromStream rd(f);
        IniReaderFromStream::Row rw;

        while (rd.next(rw)) {
            if (rw.section.empty() && rw.key == "include") {
                load_strategy_config_map(base_path / rw.value, mp, processed);
            } else if (rw.section.empty()){
                mp.set(std::string(rw.key), std::string(rw.value), base_path);
            } else {
                mp.set(std::format("{}#{}",rw.section, rw.key), std::string(rw.value), base_path);
            }
        }
    }

    std::shared_ptr<ConfigBackendWithPathsMap> load_strategy_config_as_map(const std::filesystem::path &path) {
        auto mp = std::make_shared<ConfigBackendWithPathsMap>();
        std::unordered_set<std::filesystem::path> processed;
        load_strategy_config_map(path, *mp, processed);
        return mp;

    }
    StrategyConfig load_strategy_config(const std::filesystem::path &path) {
        return StrategyConfig(ConfigBackendWithPaths(load_strategy_config_as_map(path)), '#');

    }

}