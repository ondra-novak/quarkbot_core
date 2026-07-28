#pragma once

#include <chrono>
namespace quarkbot {

using Timestamp = std::chrono::system_clock::time_point;


constexpr Timestamp interval_lower_bound(Timestamp tp, Timestamp::duration duration)    {
    auto r = (tp.time_since_epoch().count() / duration.count()) * duration.count();
    return Timestamp(Timestamp::duration(r));    
}

constexpr Timestamp interval_upper_bound(Timestamp tp, Timestamp::duration duration)  {
    return interval_lower_bound(tp, duration)+duration;
}


}