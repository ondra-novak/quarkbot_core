#pragma once

#include "../shared/counter.h"
#include <memory>
#include <cmath>

namespace quarkbot {



class NumericPrimitiveFunction { // @suppress("Miss copy constructor or assignment operator")
public:
    using Real = double;

    NumericPrimitiveFunction(Real min_x, Real max_x, const Real *values, int count)
        :_min_x(min_x)
        ,_dx((max_x - min_x)/static_cast<Real>(count))
        , _values(values), _count(count) {}


    Real get_value(Real x) const {
        Real y1;
        Real y2;
        Real y3;
        Real y4;
        Real x1;
        Real x2;
        Real x3;
        Real x4;
        Real fpos = (x - _min_x)/_dx;
        Real fidx = std::floor(fpos);
        int idx = static_cast<int>(fidx);
        if (idx < 1) {
            y1 = 0;
            y2 = _values[0];
            y3 = _values[1];
            y4 = _values[2];
            x2 = _min_x+_dx;
        } else if (idx > _count-2) {
            y1 = _values[_count-4];
            y2 = _values[_count-3];
            y3 = _values[_count-2];
            y4 = _values[_count-1];
            x2 = _min_x+_dx*(_count-2);
        } else {
            y1 = _values[idx-2];
            y2 = _values[idx-1];
            y3 = _values[idx];
            y4 = _values[idx+1];
            x2 = _min_x+_dx*fidx;
        }
        x1 = x2 - _dx;
        x3 = x2 + _dx;
        x4 = x2 + 2*_dx;
        auto xx1 = x-x1;
        auto xx2 = x-x2;
        auto xx3 = x-x3;
        auto xx4 = x-x4;
       return y1 * ((xx2)*(xx3)*(xx4))/((x1-x2)*(x1-x3)*(x1-x4))
             +y2 * ((xx1)*(xx3)*(xx4))/((x2-x1)*(x2-x3)*(x2-x4))
             +y3 * ((xx1)*(xx2)*(xx4))/((x3-x1)*(x3-x2)*(x3-x4))
             +y4 * ((xx1)*(xx2)*(xx3))/((x4-x1)*(x4-x2)*(x4-x3));
    }

protected:
    Real _min_x;
    Real _dx;
    const Real * _values;
    int _count;

};

template<unsigned int N>
class NumericPrimitiveFunctionStatic: public NumericPrimitiveFunction {
public:

    using Real = double;
    using Super = NumericPrimitiveFunction;



    template<std::invocable<Real> Fn>
    NumericPrimitiveFunctionStatic(Real min_x, Real max_x, Fn &&fn)
        :Super(min_x, max_x, _values, N) {


        for (int i = 0; i < static_cast<int>(N); ++i) {
            Real x1 = Super::_min_x + i * Super::_dx;
            Real y1 = std::forward<Fn>(fn)(x1);
            Real x2= x1+Super::_dx;
            Real y2 = std::forward<Fn>(fn)(x2);
            Real x3= x1+Super::_dx*0.5;
            Real y3 = std::forward<Fn>(fn)(x3);
            if (!std::isfinite(y1)) {
                if (!std::isfinite(y2)) {
                    y1 = y2 = static_cast<Real>(0);
                } else {
                    y1 = y2;
                }
            } else if (!std::isfinite(y2)) {
                y2 = y1;
            }
            Real p = Super::_dx/static_cast<Real>(6)
                    * (y1 + y2 + static_cast<Real>(4)*y3);
            _values[i] = p;
        }
        Counter<Real> cnt = {};
        for (Real &x: _values) {
            cnt += x;
            x = cnt.apr_value();
        }
    }

protected:
    Real _values[N];
};

///Create primitive function (integration) of given function
/**
 *
 * @tparam N count of pre-calculated points in numeric integration
 * @param range_min range low value
 * @param range_max range high value;
 * @param fn function y = fn(x) to integrate.
 * @return a function, which returns a aproximate value of primitive function which
 * is result of integration of fn(x) at given range. To calculate integral value you
 * need subtract result of function for low value of the range from result
 * of function for high value of the range
 */
template<std::size_t N, std::invocable<double> Fn>
auto make_primitive_fn(double range_min, double range_max, Fn &&fn) {
    static_assert(std::is_convertible_v<std::invoke_result_t<Fn, double>, double>);
    auto tbl = std::make_shared<NumericPrimitiveFunctionStatic<N> >(range_min, range_max, std::forward<Fn>(fn));
    return [tbl](double x) {
        return tbl->get_value(x);
    };
}


}
