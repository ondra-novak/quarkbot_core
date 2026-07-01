#pragma once

#include "quarkbot/context.hpp"

namespace quarkbot_test {

///Extended context with no additional members, used to verify that the loader
///distinguishes strategies by context type even when the derived context adds nothing
struct FooContext : quarkbot::StrategyContext {};

}
