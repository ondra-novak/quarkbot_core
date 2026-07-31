#pragma once

#include "quarkbot/serie.hpp"
#include <cstddef>
namespace quarkbot {

namespace ta {

template<IsSerie Serie>
class EMA {
public:

    using SerieType = Serie;
    using DataPoint = typename Serie::value_type;

    
    static_assert(BasicMathType<DataPoint>);


    EMA(Serie serie, std::size_t interval):_serie(std::move(serie)), 
        _alpha(2.0/static_cast<double>(interval+1)) {

        _serie.reserve(1);
        _value = _serie[0];
    }

    DataPoint update(const DataPoint &new_value) {
        
        if (_value.has_value()) {
            _value = *_value + (new_value -*_value) * static_cast<DataPoint>(_alpha);            
        } else {
            _value = new_value;
        }
        DataPoint out = *_value;
        _serie.put(out);
        return out;    
    }

    explicit operator bool() const {return _value.has_value();}

protected:

    Serie _serie;
    std::optional<DataPoint> _value = {};
    double _alpha = {};

};

}
}