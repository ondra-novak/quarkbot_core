#pragma once

#include "quarkbot/defs.hpp"
#include <type_traits>
namespace quarkbot {

namespace ta {

///Simple Moving Average
/**
@tparam Serie type of serie to store values. This type also carries type of data point. 
Serie must be a serie of basic math type (int, float, double, decimal, etc)
 */
template<IsSerie Serie>
class SMA {
public:

    using SerieType = Serie;
    using DataPoint = typename Serie::value_type;

    static_assert(BasicMathType<DataPoint>);

    ///Construct SMA indicator
    /**
        @param serie serie to store values. The serie must be able to store at least interval values
        @param interval number of values to average. The indicator will return average of last interval values
    */
    SMA(Serie serie, std::size_t interval)
        :_serie(std::move(serie)), _interval(interval) {
            _sum = DataPoint{};
            _serie.reserve(interval);
            for (_cur_len = 0; _cur_len < interval; ++_cur_len) {
                auto tp = _serie[_cur_len];
                if (!tp) break;
                _sum += *tp;
            }            
        }

    ///Update SMA with new value
    /**
        @param value new value to update SMA
        @return current SMA value
        @note during warmup phase (when less than interval values are available), the returned value is average of all available values. 
        After warmup phase, the returned value is average of last interval values
    */
    DataPoint update(DataPoint value) {
        if (_cur_len < _interval) {
            _sum += value;
            _serie.add(value);
            ++_cur_len;
        } else {
            DataPoint last = _serie[_interval-1].value_or(DataPoint{});
        
            _sum += value - last;
            _serie.add(value);
        }
        return _sum/static_cast<DataPoint>(_cur_len);
    }

    ///returns true, if returned value is accurate (returns false during warmup phase)
    explicit operator bool() const {
        return _cur_len == _interval;
    }    

protected:
    Serie _serie;
    std::size_t _interval;
    std::size_t _cur_len;
    DataPoint _sum;
};


}





}