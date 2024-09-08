#pragma once

#include "../series.h"

namespace quarkbot {




template<typename T, typename Series = LocalSeries<T> >
class SMA {
public:

    SMA(unsigned int bars):_bars(bars) {}
    SMA(unsigned int bars, Series series):_bars(bars), _series(std::move(series)) {}
    template<typename ... Args> requires(std::is_constructible_v<Series,Args...>)
    SMA(unsigned int bars, Args && ... args):_bars(bars), _series(std::forward<Args>(args)...) {}

    void update(T value) {
        if (!_initialized) {
            _sum = T();
            for (const T &val: _series) {
                _sum += val;
            }
            _initialized = true;
        }
        _series.push(value);
        _sum += value;
        while (_series.size()>_bars) {
            _sum -= _series.back();
            _series.pop();
        }
    }



    T value() const {

        return _sum/static_cast<T>(_series.size());
    }

protected:
    Series _series;
    T _sum = {};
    unsigned int _bars;
    bool _initialized = false;
};



}
