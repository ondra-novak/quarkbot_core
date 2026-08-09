#pragma once

#include "quarkbot/context.hpp"
#include <filesystem>
namespace quarkbot {

using StrategyConfig = StrategyContext::Config;


StrategyConfig load_strategy_config(const std::filesystem::path &path);

}