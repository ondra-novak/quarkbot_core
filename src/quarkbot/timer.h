#pragma once

#include "function.h"

#include <chrono>


namespace quarkbot {


using TimerID = std::intptr_t;
using Timestamp = std::chrono::system_clock::time_point;
using TimeSpan = std::chrono::system_clock::duration;

using TimerEventCB = Function<void()>;

}
