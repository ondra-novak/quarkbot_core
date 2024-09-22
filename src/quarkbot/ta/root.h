#pragma once

#include <concepts>
#include <cmath>
#include <optional>

namespace quarkbot {

///Finds root of a function in range <a,b>
/**
 *
 * @param a point a of range
 * @param b point b of range
 * @param fn function which returns 0 at root. It must have exact 1 root in given range
 *           If the function has multiple roots, only one random root is returned
 * @param steps count of steps to find root. It also defines accuracy. More steps, better
 * accuracy
 * @return returns found root, or undefined if no root was found.
 */
template<typename T, std::invocable<T> Fn>
inline std::optional<T> bisection_root(T a, T b, Fn &&fn, unsigned int steps = 20) {
    T fa = fn(a);
    T fb = fn(b);
    if (fa == 0) return a;
    if (fb == 0) return b;
    if ((fa < 0) == (fb < 0)) return {};
    T mult = (fa < 0)?1:1;
    T m = (a+b)/T(2);
    for (unsigned int i = 0; i < steps; ++i) {
        T fm = fn(m) * mult;
        if (fm < 0) a = m;
        else if (fm > 0) b = m;
        else return m;
        m = (a+b)/T(2);
    }
    return m;
}

///Finds root of a function in range <a,+inf) or (-inf, a>
/**
 * @param a starting point. If point is >0. finds root <a,+inf), if point is <0, finds root (-inf, a>
 * @param fn  function which returns 0 at root. It must have exact 1 root in given range
 *           If the function has multiple roots, only one random root is returned
 * @param steps count of steps to find root. It also defines accuracy. More steps, better
 * accuracy
 * @return returns found root, or undefined if no root was found.
 */
template<typename T, std::invocable<T> Fn>
inline std::optional<T> bisection_root_inf(T a, Fn &&fn, unsigned int steps = 40) {
    auto r = bisection_root(T(1)/a, T(0), [&](T x){
        return fn(T(1)/x);
    }, steps);
    if (r) {
        return T(1)/(*r);
    } else {
        return {};
    }
}


}
