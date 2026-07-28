#pragma once

#include "quarkbot/defs.hpp"
#include "quarkbot/ta/ema.hpp"
#include "quarkbot/ta/ma.hpp"
#include <utility>
namespace quarkbot {
namespace ta {

template<MovingAverage _MA>
class MACDGen {
public:

    using DataPoint = typename _MA::DataPoint;
    using Serie = typename _MA::SerieType;
    using SerieType = typename _MA::SerieType;

    struct Result {
        DataPoint macd;
        DataPoint signal;
    };

    struct AllIndicators {
        _MA slow;
        _MA fast;
        _MA signal;
    };


    MACDGen(Serie serie, std::size_t slow=26, std::size_t fast=12, std::size_t signal=9)
        :_ind(init_indicators(std::move(serie), slow, fast, signal)) 

    {

    }
    Result update(const DataPoint &dp) {
        DataPoint r = _ind.fast.update(dp) - _ind.slow.update(dp);
        DataPoint s = _ind.signal.update(r);
        return {r,s};
    }
protected:
    AllIndicators _ind;

    static AllIndicators init_indicators(Serie serie, std::size_t slow, std::size_t fast, std::size_t signal) {
        Serie clone1 = serie.clone();
        Serie clone2 = clone1.clone();
        return {_MA(std::move(serie), slow),
                _MA(std::move(clone1), fast),
                _MA(std::move(clone2), signal),
            };
    }
};

template<IsSerie Serie>
using MACD = MACDGen<EMA<Serie> >;

}
}