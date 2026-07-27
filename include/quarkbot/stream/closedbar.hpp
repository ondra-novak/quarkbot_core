#pragma once

#include "../types.hpp"
#include "trade.hpp"
#include <chrono>


namespace quarkbot {
    
struct ClosedBar  {
    struct MarketInstrumentStream {};
    static constexpr std::size_t not_available = 0;

    Decimal open = not_available;
    Decimal high = not_available;
    Decimal low = not_available;
    Decimal close = not_available;
    Decimal volume = not_available; 
    std::size_t trades = not_available;   
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    
    using Param = unsigned int;

    ClosedBar &view() {return *this;}

    static std::size_t to_interval_index(std::chrono::system_clock::time_point tp, unsigned int interval_sec) {
        return static_cast<std::size_t>(std::chrono::system_clock::to_time_t(tp)/interval_sec);
    }
    static std::chrono::system_clock::time_point interval_lower_bound(std::chrono::system_clock::time_point tp,unsigned int interval_sec) {
        auto tim = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
        auto adjtim = (tim / interval_sec) * interval_sec;
        return std::chrono::system_clock::time_point(std::chrono::seconds(adjtim));
    }
    static std::chrono::system_clock::time_point interval_upper_bound(std::chrono::system_clock::time_point tp,unsigned int interval_sec) {
        return interval_lower_bound(tp, interval_sec) + std::chrono::seconds(interval_sec);
    }



    ClosedBar add(const Trade &tr, unsigned int interval) const {
        auto lb = interval_lower_bound(tr.time, interval);
        if (lb != start_time) {
            return {
                tr.price,tr.price,tr.price,tr.price,
                tr.size,1,start_time, start_time+std::chrono::seconds(interval)
            };
        } else {
            return {
                 open, std::max(high,tr.price), std::min(low,tr.price), tr.price, 
                volume + tr.size, trades+1, start_time, end_time
            };
        }
    }
};

template<unsigned int _interval_sec>
struct ClosedBarInterval: ClosedBar{
    constexpr static Param param = _interval_sec;
    ClosedBarInterval add(const Trade &tr) const {
        return ClosedBar::add(tr, _interval_sec);
    }
};

}