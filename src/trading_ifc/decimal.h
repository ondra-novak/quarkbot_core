#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>
#include <vector>
#include <limits>
#include <string_view>

namespace trading_api {



class Decimal {
public:
#if __cpp_lib_constexpr_cmath > 202306L
    static constexpr bool cmath_is_const = true;
#else
    static constexpr bool cmath_is_const = false;
#endif
    static constexpr int mantisa_shift = 8;
    static constexpr int mantisa_bits = (sizeof(std::uint64_t)*8 - mantisa_shift);
    static constexpr int exponent_mask = 0xFF;
    static constexpr int exponent_ofs = exponent_mask>>1;
    static constexpr std::int64_t mantisa_max = (1LL << mantisa_bits) - 1;
    static constexpr std::int64_t mantisa_half =  (1LL << (mantisa_bits/2)) - 1;


    constexpr Decimal():_num_data(make_number(0,0)) {}
    constexpr Decimal(std::int64_t mantisa, int exponent)
        :_num_data(make_number(mantisa, exponent)) {}
    template<typename T> requires (std::is_floating_point_v<T>)
    constexpr Decimal(T v)
        :_num_data(double2raw(v)) {}
    template<typename T> requires (std::is_integral_v<T>)
    constexpr Decimal(T v)
        :_num_data(integral2raw(v)) {}
    template<typename T> requires (std::is_enum_v<T>)
    constexpr Decimal(T v)
        :_num_data(integral2raw(static_cast<std::underlying_type_t<T> >(v))) {}
    constexpr Decimal(std::string_view v)
        :_num_data(string2raw(v)) {}

    static constexpr Decimal nan()  {
        return Decimal(0, -exponent_ofs);
    }

    friend constexpr Decimal operator+(const Decimal &a, const Decimal &b) {
        auto am = a.get_mantisa();
        auto ae = a.get_exponent();
        auto bm = b.get_mantisa();
        auto be = b.get_exponent();
        if (ae < be) {
            if (can_dec_exp(bm)) return a + b.adjust_exponent(be-1);
            else return a.adjust_exponent(be) + b;
        }
        else if (ae > be) {
            if (can_dec_exp(am)) return a.adjust_exponent(ae-1)+ b;
            return a + b.adjust_exponent(ae);
        }
        else {
            auto r = am+bm;
            if (must_inc_exp(r)) {
                return Decimal(r/10, ae+1);
            } else {
                return Decimal(r, ae);
            }
        }
    }

    friend constexpr Decimal operator-(const Decimal &a, const Decimal &b) {
        return a + (-b);
    }

    friend constexpr Decimal operator*(const Decimal &a, const Decimal &b) {
        auto am = a.get_mantisa();
        auto ae = a.get_exponent();
        auto bm = b.get_mantisa();
        auto be = b.get_exponent();
        auto bca = count_bits(am);
        auto bcb = count_bits(bm);
        while (bca+bcb >= mantisa_bits) {
            if (bcb>bca) {
                bm=bm/10;
                ++be;
                bcb = count_bits(bm);
            } else {
                am=am/10;
                ++ae;
                bca = count_bits(am);
            }
        }
        auto res = am * bm;
        return Decimal(res, ae+be);
    }

    friend constexpr Decimal operator/(const Decimal &a, const Decimal &b) {
        auto bm = b.get_mantisa();
        auto am = a.get_mantisa();
        auto ae = a.get_exponent();
        auto be = b.get_exponent();
        while (can_dec_exp(am)) {
            am*=10;
            --ae;
        }
        while (can_inc_exp_for_div(bm)) {
            bm = bm/10;
            ++be;
        }
        if (!bm) return Decimal::nan();
        auto res = am / bm;
        return Decimal(res, ae-be);
    }


    constexpr Decimal &operator+=(const Decimal &a) {
        (*this) = (*this) + a;
        return *this;
    }

    constexpr Decimal &operator-=(const Decimal &a) {
        (*this) = (*this) - a;
        return *this;
    }

    friend constexpr Decimal operator-(const Decimal &a) {
        return Decimal(-a.get_mantisa(), a.get_exponent());
    }

    friend constexpr bool operator==(const Decimal &a, const Decimal &b) {
        auto r = a - b;
        return (r.get_mantisa() == 0);
    }

    friend constexpr std::strong_ordering operator<=>(const Decimal &a, const Decimal &b) {
        auto r = a - b;
        auto m = r.get_mantisa();
        if (m < 0) return std::strong_ordering::less;
        else if (m > 0) return std::strong_ordering::greater;
        else return std::strong_ordering::equal;
    }


    enum class OutputMode {
        autoselect,
        decimals,
        scientific
    };

    template<std::output_iterator<char> Iter>
    constexpr Iter to_string(Iter iter, OutputMode mode = OutputMode::autoselect) const {
        auto e = get_exponent();
        auto m = get_mantisa();
        if (!m) {
            if (e == -exponent_ofs) {
                constexpr std::string_view nan = "nan";
                return std::copy(nan.begin(), nan.end(), iter);
            } else {
                *iter = '0';++iter;
                return iter;
            }
        }
        if (m < 0) {
            *iter = '-';++iter;
            m = -m;
        }
        if (e == exponent_ofs+1) {
                constexpr std::string_view nan = "inf";
                return std::copy(nan.begin(), nan.end(), iter);
        }
        std::string mstr = std::to_string(m);
        auto mtst = m;
        auto E = e;
        while (mtst > 9) {
            mtst/=10;
            E++;
        }
        bool scnt;
        switch (mode) {
            case OutputMode::scientific: scnt = true; break;
            case OutputMode::decimals: scnt = false; break;
            default: scnt = E > 8 || E < -3;break;
        };
        while (mstr.back() == '0')  {
            mstr.pop_back();
            ++e;
        }
        auto bstr = mstr.begin();
        auto estr = mstr.end();
        if (scnt) {
            *iter = *bstr;
            ++iter; ++bstr;
            if (bstr != estr) {
                *iter = '.'; ++iter;
                iter = std::copy(bstr, estr, iter);
            }
            *iter = 'E'; ++iter;
            mstr = std::to_string(E);
            iter = std::copy(mstr.begin(), mstr.end(),  iter);
            return iter;
        } else {
            int dot = mstr.size() + e;
            if (dot <= 0) {
                *iter = '0'; ++iter;
                if (dot < 0) {
                    *iter = '.'; ++iter;
                    while (dot < 0) {
                        *iter = '0'; ++iter;
                        ++dot;
                    }
                }
            }
            auto dotpos = estr;
            std::advance(dotpos, e);
            while (bstr != estr) {
                if (bstr == dotpos) {
                    *iter = '.';++iter;
                }
                *iter = *bstr;
                ++bstr; ++iter;
            }
            while (e > 0) {
                *iter = '0';++iter;
                --e;
            }
            return iter;
        }
    }
#if __cpp_lib_constexpr_string >= 201907L
    constexpr
#endif
    std::string to_string(OutputMode md = OutputMode::autoselect)const {
        std::string out;
        to_string(std::back_inserter(out), md);
        return out;
    }


    friend std::ostream &operator<<(std::ostream &os, const Decimal &n) {
        std::ios::fmtflags f = os.flags();

        OutputMode mode = OutputMode::autoselect;
        if (f & std::ios::scientific) {
            mode = OutputMode::scientific;
        } else if (f & std::ios::fixed) {
            mode = OutputMode::decimals;
        }
        n.to_string(std::ostreambuf_iterator<char>(os), mode);
        return os;
    }

    friend constexpr Decimal floor(const Decimal &n, int decimals = 0) {
        auto e = n.get_exponent();
        auto sf = -e-decimals;
        if (sf <= 0) return n;
        if (sf > 17) return Decimal();
        auto m = n.get_mantisa();
        auto dv = pow10(sf);
        if ((m < 0) && (m % dv)) {
            m = (m / dv) - 1;
        } else {
            m = m/dv;
        }
        return Decimal(m, -decimals);
    }

    friend constexpr Decimal ceil(const Decimal &n, int decimals = 0) {
        auto e = n.get_exponent();
        auto sf = -e-decimals;
        if (sf <= 0) return n;
        if (sf > 17) return Decimal();
        auto m = n.get_mantisa();
        auto dv = pow10(sf);
        if ((m > 0) && (m % dv)) {
            m = (m / dv) + 1;
        } else {
            m = m/dv;
        }
        return Decimal(m, -decimals);
    }

    friend constexpr Decimal round(const Decimal &n, int decimals = 0) {
        auto e = n.get_exponent();
        auto sf = -e-decimals;
        if (sf <= 0) return n;
        if (sf > 17) return Decimal();
        auto m = n.get_mantisa();
        auto dv = pow10(sf);
        auto hf = dv/2;
        m = (m + hf)/dv;
        return Decimal(m, -decimals);
    }

    friend constexpr Decimal abs(const Decimal &n) {
        if (n._num_data < 0) {
            return Decimal(-n.get_mantisa(), n.get_exponent());
        } else {
            return n;
        }
    }


    constexpr Decimal adjust_exponent(int new_exponent) const {
        auto m = get_mantisa();
        auto old_e = get_exponent();
        auto expdiff = new_exponent - old_e;
        if (expdiff < 0) {
            m = m * pow10(-expdiff);
        } else {
            m = m / pow10(expdiff);
        }
        return Decimal(m, new_exponent);
    }

    constexpr std::int64_t get_mantisa() const {
        return _num_data>>mantisa_shift;
    }
    constexpr int get_exponent() const {
        return static_cast<int>(_num_data & exponent_mask)-exponent_ofs;
    }

    template<typename T>
    constexpr T as() const {
        if constexpr(std::is_same_v<T, Decimal>) {
            return *this;
        } else if constexpr(std::is_integral_v<T> || std::is_enum_v<T>) {
            auto n = round(*this).adjust_exponent(0).get_mantisa();
            return static_cast<T>(n);
        } else {
            static_assert(std::is_floating_point_v<T>,"Only numerics are supported");
            return static_cast<T>(get_mantisa()) * pow(T(10),get_exponent());
        }
    }

    template<typename T> requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
    explicit operator T() const {
        return as<T>();
    }


    friend constexpr bool is_nan(const Decimal &n) {
        return n.get_exponent() == -exponent_ofs && n.get_mantisa() == 0;
    }

    friend constexpr bool is_finite(const Decimal &n) {
        return n.get_exponent() <= exponent_ofs;
    }

    friend constexpr int sgn(const Decimal &n) {
        return n._num_data <0?-1:n._num_data>0?1:0;
    }

    static constexpr Decimal inf() {
        return Decimal(mantisa_max, exponent_ofs+1);
    }

protected:
    std::int64_t _num_data;


    constexpr static std::int64_t make_number(std::int64_t value, int exponent) {
        if (std::is_constant_evaluated()) {
            if (value >= mantisa_max || value <= -mantisa_max) throw "mantisa overflow";
            if (exponent > exponent_ofs || exponent < -exponent_ofs) throw "exponent overflow";
        } else {
            if (exponent > exponent_ofs) {
                if (value) {
                    exponent = exponent_ofs+1;
                    value = (mantisa_max>>1)-(value>0);
                } else {
                    exponent = 0;
                }
            }
        }
        auto eadj = static_cast<std::int64_t>(exponent+exponent_ofs) & exponent_mask;
        return (value << mantisa_shift) | eadj;
    }

    static constexpr int count_bits(std::int64_t number) {
        #if defined(__GNUC__) || defined(__clang__)
            return 64 - __builtin_clrsbll(number);
        #else
            if (number < 0) number = -number;
            int bits = 1;
            while (number != 0) {
                number >>= 1;
                bits++;
            }
            return bits;
        #endif
    }

    static constexpr std::int64_t pow10(unsigned int exp) {
        std::int64_t x = 1;
        while (exp) {x = x * 10; --exp;}
        return x;
    }

    template<typename T>
    static constexpr T pow_cont(T base, int exponent) {
         if (exponent == 0) return 1.0;
         if (exponent % 2 == 0) {
             T halfPower = pow_cont(base, exponent / 2);
             return halfPower * halfPower;
         } else {
             return base * pow_cont(base, exponent - 1);
         }
     }

    template<typename T>
    static constexpr T pow(T base, int exponent) {
        if constexpr(cmath_is_const) {
            return std::pow(base, exponent);
        } else {
            if (std::is_constant_evaluated()) {
                if (exponent == 1) return base;
                if (exponent < 0) {
                    base = 1.0 / base;
                    exponent = -exponent;
                }
                return pow_cont(base, exponent);
            } else {
                return std::pow(base, exponent);
            }
        }
     }


    template<typename T>
    static constexpr int log10(T number)  {
        if constexpr(cmath_is_const) {
            return static_cast<int>(std::log10(number));
        } else {
            if (std::is_constant_evaluated()) {
                if (number > 0.0) {
                    auto low = std::numeric_limits<T>::min_exponent10;
                    auto minexp = std::numeric_limits<T>::max_exponent10+1;
                    auto high = minexp;
                    while (low < high) {
                        auto mid = (low+ high - 2*minexp)/2 + minexp;
                        auto v = pow<T>(10,mid);
                        auto adj = number/v;
                        if (adj < 1.0) high = mid;
                        else if (adj >= 10.0) low = mid+1;
                        else return mid;
                    }
                    return low;
                } else {
                    return 0;
                }
            } else {
                return static_cast<int>(std::log10(number));
            }
        }
    }

    static constexpr double abs(double x) {
        return x<0?-x:x;
    }

    template<typename T>
    static constexpr std::int64_t double2raw(T v) {
        if (!v) return make_number(0, 0);
        T r (v < 0?-0.5:0.5);
        int exponent = static_cast<int>(log10(abs(v))) - std::numeric_limits<T>::max_digits10 + 2;
        T base = pow(T(10), exponent);
        T num = (v/base)+r;
        std::int64_t n = static_cast<std::int64_t>(num);
        return make_number(n, exponent);
    }

    template<typename T>
    static constexpr std::int64_t integral2raw(T v) {
        if constexpr(std::is_unsigned_v<T>) {
            using C = std::common_type_t<T, std::uint64_t>;
            int e = 0;
            while (static_cast<C>(v) >= static_cast<C>(mantisa_max)) {
                ++e;
                v = v/10;
            }
            return make_number(static_cast<std::int64_t>(v),e);
        } else {
            using C = std::common_type_t<T, std::int64_t>;
            int e = 0;
            while (static_cast<C>(v) >= static_cast<C>(mantisa_max) || static_cast<C>(v) <= -static_cast<C>(mantisa_max)) {
                ++e;
                v = v/10;
            }
            return make_number(static_cast<std::int64_t>(v),e);
        }
    }

    static constexpr std::int64_t string2raw(std::string_view number) {
        bool neg = false;
        if (number.empty()) return 0;
        if (number.front() == '-') {
            neg = true;
            number = number.substr(1);
        } else if (number.front() == '+') {
            neg = false;
            number = number.substr(1);
        }
        if (number.empty()) return 0;
        int exp = 0;
        std::int64_t m = 0;
        auto b = number.begin();
        auto e = number.end();
        char c = 0;
        while (b != e) {
            c= *b;
            if (c>='0' && c<='9') {
                if (exp) ++exp;
                else {
                    m = m * 10 + (c - '0');
                    if (must_inc_exp(m)) {
                        m/=10;
                        ++exp;
                    }
                }
            } else if (c == '.' || c == 'e' || c == 'E') {
                break;
            } else {
                return 0;
            }
            ++b;
        }
        if (c == '.') {
            ++b;
            while (b != e) {
                c = *b;
                if (c>='0' && c<='9') {
                    if (exp <= 0) {
                        m = m * 10 + (c - '0');
                        if (must_inc_exp(m))  {
                            m/=10;
                        } else {
                            --exp;
                        }
                    }
                } else if (c == 'e' || c == 'E') {
                    break;
                } else {
                    return 0;
                }
                ++b;
            }
        }
        if (c == 'e' || c == 'E') {
            ++b;
            bool exp_neg = false;
            if (b != e && *b == '-') {
                exp_neg = true;
                ++b;
            } else if (b != e && *b == '+') {
                ++b;
            }
            int ee = 0;
            while (b != e) {
                c = *b;
                if (c>='0' && c<='9') {
                    ee = ee * 10 + (c - '0');
                } else {
                    return 0;
                }
                ++b;
            }
            if (exp_neg) ee = -ee;
            exp += ee;
        }
        if (b == e)  {
            if (neg) m = -m;
            return make_number(m, exp);
        } else {
            return 0;
        }
    }

    static constexpr bool can_dec_exp(std::int64_t mantisa) {
        return mantisa && mantisa < mantisa_max/10 && mantisa>-mantisa_max/10;
    }
    static constexpr bool can_inc_exp_for_div(std::int64_t mantisa) {
        return mantisa && ((mantisa % 10) == 0  || mantisa > mantisa_half || mantisa < -mantisa_half);
    }
    static constexpr bool must_inc_exp(std::int64_t mantisa) {
        return mantisa >= mantisa_max || mantisa <= -mantisa_max;
    }

};


constexpr Decimal operator"" _dec(const char *x) {
    return Decimal(std::string_view(x));
}

}
