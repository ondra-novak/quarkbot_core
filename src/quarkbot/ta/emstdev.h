#ifndef SRC_MAIN_EMSTDEV_H_
#define SRC_MAIN_EMSTDEV_H_

#include "ema.h"

namespace quarkbot {

template<typename T = Decimal>
class EMStDev {
public:

    using value_type = T;

    void set_period(unsigned int period) {
        _mean.set_period(period);
        _variance.set_period(period);
    }
    void set_period(unsigned int mean_period, unsigned int var_period) {
        _mean.set_period(mean_period);
        _variance.set_period(var_period);
    }
    void update(T value) {
        _mean.update(value);
        auto m = _mean.value();
        _variance.update(pow2(value - m));
    }

    T mean() const {
        return _mean.value();;
    }

    T stdev() const {
        return static_cast<T>(std::sqrt(static_cast<double>(_variance.value())));
    }

    std::size_t max_count() const {
        return 1;
    }

    auto get_mean() const {
        return _mean.value();
    }

    auto get_stdev() const {
        return _variance.value();
    }

    auto operator()(T x) const {
        return get_mean() + get_stdev() * x;
    }

    void set_initial(T mean, T stdev) {
        _mean.set_initial(mean);
        _variance.set_initial(pow2(stdev));
    }


protected:
    EMA<T> _mean;
    EMA<T> _variance;

    static T pow2(T x) {
        return x*x;
    }
};


}
#endif /* SRC_MAIN_EMSTDEV_H_ */
