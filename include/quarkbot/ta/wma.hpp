#pragma once

#include "quarkbot/defs.hpp"
namespace quarkbot {

namespace ta{



template<IsSerie Serie>
class WMA {
public:
    using DataPoint = typename Serie::value_type;
    using SerieType = Serie;

    WMA(SerieType serie, size_t n) : period(n), window(std::move(serie)) {
        window.reserve(period);
        current_sma_sum = {};
        wma_sum = {};
        // restore state from a (possibly persistent) serie that already holds values;
        // window[0] is the most recent value, window[cur_size-1] the oldest
        for (cur_size = 0; cur_size < period; ++cur_size) {
            auto p = window[cur_size];
            if (!p) break;
            current_sma_sum += *p;
        }
        // rebuild the weighted numerator: value at index i carries weight (cur_size - i)
        for (std::size_t i = 0; i < cur_size; ++i) {
            wma_sum += static_cast<DataPoint>(cur_size - i) * *window[i];
        }
    }

    DataPoint update(DataPoint new_price) {
        
        if (cur_size < period) {
            ++cur_size;
            size_t k = cur_size;
            current_sma_sum += new_price;
            wma_sum += k * new_price;
            
            DataPoint divisor = static_cast<DataPoint>(k * (k + 1)) / 2.0;
            window.add(new_price);
            return wma_sum / divisor;
        } else {
            DataPoint old_price = window[period-1].value_or(DataPoint{});
            window.add(new_price);

            wma_sum = wma_sum + (period * new_price) - current_sma_sum;

            current_sma_sum = current_sma_sum - old_price + new_price;

            DataPoint divisor = static_cast<DataPoint>(period * (period + 1)) / 2.0;
            return wma_sum / divisor;
        }
    }

    ///returns true, if returned value is accurate (returns false during warmup phase)
    explicit operator bool() const {
        return cur_size >= period;
    }

private:
    std::size_t period;
    std::size_t cur_size;
    Serie window;
    DataPoint wma_sum = {};     // Průběžný čitatel pro WMA
    DataPoint current_sma_sum = {}; // Simulace vnitřní sumy vašeho SMA

};


}
}