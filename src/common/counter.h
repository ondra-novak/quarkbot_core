#pragma once

namespace trading_api {


///A counter with increased accuracy
template<typename T>
class Counter {
public:

    Counter() = default;
    Counter(T high, T low):_high(high),_low(low) {}
    Counter operator+(const T &val) const {
        T ls = _low + val;
        T hs = _high + ls;
        auto hinc = hs - _high;
        return Counter(hs, ls - hinc);
    }
    Counter &operator+=(const T &val) {
        T prevh = _high;
       _low += val;
       _high += _low;
       _low -= (_high - prevh);
       return *this;
    }

    T operator-(const Counter &other) const {
        return (_high - other._high) + (_low - other._low);
    }
    T apr_value() const {return _high+_low;}

protected:
    T _high = {};
    T _low = {};

};

}
