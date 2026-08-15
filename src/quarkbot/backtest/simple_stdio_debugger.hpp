#pragma once

#include "ibacktest_debugger.hpp"
#include "quarkbot/storage.hpp"
namespace quarkbot {


std::function<void(std::shared_ptr<IBacktestDebugger>, Storage)> get_simple_stdio_debugger();

}