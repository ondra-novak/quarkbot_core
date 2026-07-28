#pragma once

#include "quarkbot/defs.hpp"
namespace quarkbot {


namespace ta {

///Calculate difference between current and history point
template<IsSerie Serie>
class Difference {
public:

    using SerieType = Serie;
    using DataPoint = typename Serie::value_type;

    Difference(Serie serie, std::size_t interval):_serie(std::move(serie)), _interval(interval)  {
        _serie.reserve(interval);
        for (_cur_len = 0; _cur_len < interval; ++_cur_len) {
            if (!_serie[_cur_len]) break;
        }
    }

    DataPoint update(const DataPoint &pt) {
        if (_cur_len == 0) {
            _serie.add(pt);
            return 0;
        } else if (_cur_len < _interval) {
            DataPoint prev_pt = _serie[_cur_len-1];
            _serie.add(pt);
            return pt - prev_pt;
        } else {
            DataPoint prev_pt = _serie[_interval-1];
            _serie.add(pt);
            return pt - prev_pt;
        }
    }

protected:
    SerieType _serie;
    std::size_t _interval;
    std::size_t _cur_len;
};

}

}