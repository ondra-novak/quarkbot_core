#pragma once

#include "../series.h"

namespace quarkbot {


template<typename T = Decimal>
class EMA {
public:

    using value_type = T;

    void set_period(unsigned int bars) {_pp1 = static_cast<T>(bars+1);}

    void update(T value) {
        if (!_initialized) {
            _cur_val = value;
        } else {
            _cur_val = calc_new_value(_cur_val, value);
        }
    }

    T value() const {
        return _cur_val;
    }

    std::size_t max_count() const {
        return 1;
    }

    void clear() {_initialized = false;}

    void set_initial(T val) {_cur_val = val;}

protected:
    bool _initialized = false;
    T _cur_val = {};
    T _pp1 = 10;
    static constexpr auto two = static_cast<T>(2);

    T calc_new_value(T prev_val, T val ) const {
        return ((val - prev_val) * 2 / _pp1) + prev_val;
    }
};




}
