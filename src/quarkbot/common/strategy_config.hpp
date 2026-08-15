#pragma once

#include "quarkbot/config_backend_with_paths.hpp"
#include "quarkbot/context.hpp"
#include <filesystem>
namespace quarkbot {

using StrategyConfig = StrategyContext::Config;


StrategyConfig load_strategy_config(const std::filesystem::path &path);
std::shared_ptr<ConfigBackendWithPathsMap> load_strategy_config_as_map(const std::filesystem::path &path);

}