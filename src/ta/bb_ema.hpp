#pragma once

#include "ta/ema.hpp"
#include <cstddef>
namespace quarkbot {

///calculates average and deviation but based on ema averages
template<typename Number>
class Bollinger_Ema {

    struct Result {
        Number mean;
        Number dev;
    };

    Bollinger_Ema(Number mean_apha, Number dev_alpha, Result initial_value)
        :_mean(mean_apha, initial_value.mean)
        ,_var(dev_alpha, initial_value.dev) {}
    Bollinger_Ema(std::size_t period, Number dev_adjust, Result initial_value)
        :Bollinger_Ema(calculate_params(period, dev_adjust, initial_value)) {}

protected:
    Ema<Number> _mean;
    Ema<Number> _var;

    static Bollinger_Ema calculate_params(std::size_t p)
};

}