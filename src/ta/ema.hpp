#pragma once

#include <cstddef>
namespace quarkbot {


template<typename Number = double>
class Ema {
public:
    Ema(Number alpha, Number initial_value):_value(initial_value),_alpha(alpha) {}
    Ema(std::size_t period, Number initial_value):Ema(Number(2)/(Number(period)+1),initial_value) {}
    Number update(Number new_value) {
        _value = _alpha * new_value + (Number(1)-_alpha)*_value;
       return _value;
    }
    Number value() const {
        return _value;
    }   
protected:
    Number _value = {};
    Number _alpha;
};

}