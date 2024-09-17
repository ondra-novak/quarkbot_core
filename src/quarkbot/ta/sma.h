#pragma once

#include <queue>
#include "../series.h"

namespace quarkbot {

template<typename T = Decimal>
class SMA {
public:


    using value_type = T;

    void set_period(unsigned int bars) {_bars = bars;}

    void clear() {_sum = {}, _data = {};}
    void update(T value) {
        _data.push(value);
        _sum += value;
        while (_data.size() > _bars) {
            _sum -= _data.front();
            _data.pop();
        }
    }


    T value() const {
        return _sum/static_cast<T>(_data.size());
    }

    std::size_t max_count() const {
        return _bars;
    }

protected:
    std::queue<T> _data;
    T _sum = {};
    unsigned int _bars = 10;
    bool _initialized = false;
};



}
