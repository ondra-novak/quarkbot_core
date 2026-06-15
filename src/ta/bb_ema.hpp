#pragma once

#include "ta/ema.hpp"
#include <cmath>
#include <cstddef>
namespace quarkbot {

///calculates average and deviation but based on ema averages
template<typename Number>
class Bollinger_Ema {
public:
    struct Result {
        Number mean = {};
        Number dev = {};
    };

    Bollinger_Ema(Number mean_alpha, Number dev_alpha, Result initial_value)
        :_mean(mean_alpha, initial_value.mean)
        ,_var(dev_alpha, initial_value.dev) {}

    static Bollinger_Ema from_period(std::size_t period, double dev_adjust, Result initial_value) {
        return Bollinger_Ema(calculate_params(period, dev_adjust, initial_value)); 
    }

    Result update(Number new_value) {
        auto mean = _mean.update(new_value);
        auto dist = new_value  - mean;
        auto r = _var.update(dist * dist);
        return {mean, Number(std::sqrt(r))};
    }
    Result value() const {
        return {_mean.value(), Number(std::sqrt(_var.value()))};
    }   
protected:
    Ema<Number> _mean;
    Ema<Number> _var;

    static Bollinger_Ema calculate_params(std::size_t p, double dev_adjust, Result initial_value) {
        double adj_p = static_cast<double>(p) * std::exp(dev_adjust);
        std::size_t q =  std::max(std::size_t(1), static_cast<std::size_t>(adj_p));
        auto mean_alpha = Number(2)/(Number(p)+1);
        auto dev_alpha = Number(2)/(Number(q)+1);
        return Bollinger_Ema(mean_alpha, dev_alpha, initial_value);    
    }   

};

}