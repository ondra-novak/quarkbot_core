#pragma once

#include "quarkbot/defs.hpp"
#include "quarkbot/ta/ema.hpp"
namespace quarkbot {

namespace ta {


template<IsSerie Serie>
class DEMA {
public:

    using SerieType = Serie;
    using DataPoint = typename EMA<Serie>::DataPoint;

    DEMA(Serie serie, std::size_t interval)
        :_e1(serie.clone(), interval), _e2(std::move(serie), interval) {}

    ///Double Exponential Moving Average: DEMA = 2*EMA(x) - EMA(EMA(x))
    DataPoint update(const DataPoint &val) {
        DataPoint e1 = _e1.update(val);
        DataPoint e2 = _e2.update(e1);
        return e1 + e1 - e2;
    }
    
    explicit operator bool() const {
        return static_cast<bool>(_e1) && static_cast<bool>(_e2);
    }    


protected:
    EMA<Serie> _e1;
    EMA<Serie> _e2;
};

}

}