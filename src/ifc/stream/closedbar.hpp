#pragma once

#include "../stream_defs.hpp"
#include "../types.hpp"
#include "trade.hpp"


namespace quarkbot {
struct ClosedBar  {
    static constexpr std::size_t not_available = 0;

    Decimal open = not_available;
    Decimal high = not_available;
    Decimal low = not_available;
    Decimal close = not_available;
    Decimal volume = not_available; 
    std::size_t trades = not_available;   
    std::size_t interval_index = not_available;
    
    ClosedBar &view() {return *this;}
    static constexpr MarketInstrumentStreamTypeItem::Type type = "closed_bar";
    using ParamType =  StreamSingleParam<unsigned int>;
    static std::size_t to_interval_index(std::chrono::system_clock::time_point tp, unsigned int interval_sec) {
        return static_cast<std::size_t>(std::chrono::system_clock::to_time_t(tp)/interval_sec);
    }

    ClosedBar add(const Trade &tr, unsigned int interval) const {
        auto index = to_interval_index(tr.time, interval);
        if (index != interval_index) {
            return {
                tr.price,tr.price,tr.price,tr.price,
                tr.size,1,index
            };
        } else {
            return {
                open, std::max(high,tr.price), std::min(low,tr.price), tr.price, 
                volume + tr.size, trades+1, index
            };
        }
    }
};

template<unsigned int _interval_sec>
struct ClosedBarInterval: ClosedBar, MarketInstrumentStreamTypeItem{
    constexpr static auto params =ParamType {{},_interval_sec};
    std::chrono::system_clock::time_point interval_begin() const {
        return std::chrono::system_clock::from_time_t(static_cast<time_t>(interval_index * _interval_sec));        
    }
    std::chrono::system_clock::time_point interval_end() const {
        return std::chrono::system_clock::from_time_t(static_cast<time_t>(interval_index * _interval_sec+1));
    }
    ClosedBarInterval add(const Trade &tr) const {
        return {
            *this,
            add(tr, _interval_sec)
        };
    }
};

}