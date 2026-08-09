#include "strategy_config.hpp"
#include "quarkbot/config.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace quarkbot {


    static void load_strategy_config_map(const std::filesystem::path &path, 
                    ConfigBackendMap &mp, std::unordered_set<std::filesystem::path> &processed) {
    
        if (!processed.insert(path).second) return;
        std::ifstream f(path);
        if (!f) throw std::runtime_error(std::format("Failed to open: {}", path.string()));
        IniReaderFromStream rd(f);
        IniReaderFromStream::Row rw;

        while (rd.next(rw)) {
            if (rw.section.empty() && rw.key == "include") {
                load_strategy_config_map(path / rw.value, mp, processed);
            } else if (rw.section.empty()){
                mp[std::string(rw.key)] = std::string(rw.value);
            } else {
                mp[std::format("{}#{}", rw.section, rw.key)] = std::string(rw.value);
            }
        }
    }

    StrategyConfig load_strategy_config(const std::filesystem::path &path) {

        auto mp = std::make_shared<ConfigBackendMap>();
        std::unordered_set<std::filesystem::path> processed;
        load_strategy_config_map(path, *mp, processed);
        return StrategyConfig({std::move(mp)}, '#');

    }

}