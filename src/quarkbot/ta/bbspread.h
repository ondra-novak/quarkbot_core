#pragma once

#include <variant>
#include "emstdev.h"

namespace quarkbot {

template<typename T = Decimal>
class BBSpread {
public:

    BBSpread() = default;
    BBSpread(const BBSpread &) = delete;
    BBSpread &operator=(const BBSpread &) = delete;
    BBSpread(BBSpread &&) = default;
    BBSpread &operator=(BBSpread &&) = default;

    struct value_type {
        T price;
        bool isfill;
    };

    struct SpreadStats {
        T spread;
        T mult_buy;
        T mult_sell;
    };

    struct Result {
        std::optional<T> buy;
        std::optional<T> sell;
    };

    void set_params(unsigned int mean_period, unsigned int var_period,
            std::span<T> curves, bool zero_line) {

        _max_count = std::max(mean_period, var_period);
        _stdev.set_period(mean_period, var_period);
        _curves.clear();
        if (curves.empty()) {
            _curves.push_back(1.0);
            _curves.push_back(-1.0);
        } else {
            for (T d : curves) {
                if (d > 0) {
                    _curves.push_back(d);
                    _curves.push_back(-d);
                }
            }
        }

        if (zero_line) _curves.push_back(0);
        std::sort(_curves.begin(), _curves.end());
    }

    void update(value_type val) {
        using namespace std;
        if (_inited) {
            if (val.isfill) {

                auto near = _disabled_curve;
                T best = numeric_limits<double>::max();
                for (const auto &c: _curves) {
                    if (&c == _disabled_curve) continue;
                    T p = _stdev(c);
                    T dist = abs(p - val.price);
                    if (dist < best) {
                        best = dist;
                        near = &c;
                    }
                }

                _disabled_curve = &(*near);
                _buy_curve = &(*near)-1;
                _sell_curve = &(*near)+1;

            } else {
                _stdev.update(val.price);
                auto nxb = next_buy(_buy_curve);
                if (_stdev(*nxb) < val.price) _buy_curve = nxb;

                auto nxs = next_sell(_sell_curve);
                if (_stdev(*nxs) > val.price) _sell_curve = nxs;

            }
        } else {
            if (_curves.empty()) throw std::runtime_error("BBSpread invalid params (set_params)");
            _stdev.set_initial(val.price,val.price/T(100));
            auto init = std::lower_bound(_curves.begin(), _curves.end(), 0.0);
            if (init == _curves.end()) {
                _disabled_curve = &_curves.back();
            } else {
                _disabled_curve = &(*init);
            }
            _buy_curve = &_curves.front();
            _sell_curve = &_curves.back();
            _inited = true;
        }

    }

    static constexpr auto infinity = std::numeric_limits<T>::infinity();

    SpreadStats get_stats(double equilibrium) const {
        SpreadStats out;
        out.mult_buy = -infinity;
        out.mult_sell = infinity;
        out.spread = 0;
        if (!_inited) return out;

        out.spread = _stdev.get_stdev()/_stdev.get_mean();
         {
             auto iter = _buy_curve;
             while (!below(iter)) {
                 auto b = _stdev(*iter);
                 if (b < equilibrium) {
                     out.mult_buy = *iter;
                     break;
                 }
                 --iter;
             }
         }

         {
             auto iter = _sell_curve;
             while (!above(iter)) {
                 auto s = _stdev(*iter);
                 if (s > equilibrium) {
                     out.mult_sell = *iter;
                     break;
                 }
                 ++iter;
             }
         }
         return out;
    }
    Result get_result(T equilibrium) const {
        Result r;
        if (!_inited) return r;

        {
            auto iter = _buy_curve;
            while (!below(iter)) {
                auto b = _stdev(*iter);
                if (b < equilibrium) {
                    r.buy = b;
                    break;
                }
                --iter;
            }
        }

        {
            auto iter = _sell_curve;
            while (!above(iter)) {
                auto s = _stdev(*iter);
                if (s > equilibrium) {
                    r.sell = s;
                    break;
                }
                ++iter;
            }
        }
        return r;
    }

    std::size_t max_count() const {
        return _max_count;
    }

    void clear() {
        _inited = false;
    }


protected:

        using Iter = const T *;

        std::vector<T> _curves;
        EMStDev<T> _stdev;
        Iter _disabled_curve;
        Iter _sell_curve;
        Iter _buy_curve;
        bool _inited = false;
        std::size_t _max_count = 1;

        Iter next_buy(Iter x) const;
        Iter next_sell(Iter x) const;
        bool below(Iter x) const;
        bool above(Iter x) const;
};

template<typename T>
inline typename BBSpread<T>::Iter BBSpread<T>::next_buy(Iter x) const {
    if (x == &_curves.back()) return x;
    auto a = x;
    ++a;
    if (a == _disabled_curve) {
        if (a == &_curves.back()) return x;
        ++a;
    }
    return a;
}

template<typename T>
inline typename BBSpread<T>::Iter BBSpread<T>::next_sell(Iter x) const {
    if (x == &_curves.front()) return x;
    auto a = x;
    --a;
    if (a == _disabled_curve) {
        if (a == &_curves.front()) return x;
        --a;
    }
    return a;
}

template<typename T>
inline bool BBSpread<T>::below(Iter x) const {
    return x < &_curves.front();
}

template<typename T>
inline bool BBSpread<T>::above(Iter x) const {
    return x > &_curves.back();
}



}
