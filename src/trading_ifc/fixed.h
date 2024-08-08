#pragma once

#include <iostream>
#include <cstdint>
#include <vector>
#include <limits>
#include <string_view>

namespace trading_api {


class FixedNumber {
public:


    constexpr explicit FixedNumber(std::int64_t raw):_num_data(raw) {}
    constexpr FixedNumber(std::int64_t mantisa, int exponent):_num_data(make_number(mantisa, exponent)) {}
    constexpr FixedNumber(double v)
        :_num_data(double2raw(v)) {}
    constexpr FixedNumber(int v)
        :_num_data(make_number(v, 0)) {}
    constexpr FixedNumber(std::string_view v)
        :_num_data(string2raw(v)) {}

    friend constexpr FixedNumber operator+(const FixedNumber &a, const FixedNumber &b) {
        if (a.get_exponent() != b.get_exponent()) {
            FixedNumber new_b = b.set_exponent(a.get_exponent());
            return a + new_b;
        }
        return FixedNumber(a.get_mantisa()+b.get_mantisa(),a.get_exponent());
    }

    friend constexpr FixedNumber operator-(const FixedNumber &a, const FixedNumber &b) {
        if (a.get_exponent() != b.get_exponent()) {
            FixedNumber new_b = b.set_exponent(a.get_exponent());
            return a - new_b;
        }
        return FixedNumber(a.get_mantisa()-b.get_mantisa(),a.get_exponent());
    }

    friend constexpr FixedNumber operator*(const FixedNumber &a, const FixedNumber &b) {
        auto am = a.get_mantisa();
        auto ae = a.get_exponent();
        auto bm = b.get_mantisa();
        auto be = b.get_exponent();
        bool an = am < 0;
        bool bn = bm < 0;
        if (an) am = -am;
        if (bn) bm = -bm;
        auto bca = bit_count(am);
        auto bcb = bit_count(bm);
        while (bca+bcb > 55) {
            if (bcb>bca) {
                bm/=10;
                ++be;
                bcb = bit_count(bm);
            } else {
                am/=10;
                ++ae;
                bca = bit_count(am);
            }
        }
        auto res = am * bm;
        if (an != bn) res = -res;
        return FixedNumber(res, ae+be);
    }

    friend constexpr FixedNumber operator/(const FixedNumber &a, const FixedNumber &b) {
        auto am = a.get_mantisa();
        auto ae = a.get_exponent();
        auto bm = b.get_mantisa();
        auto be = b.get_exponent();
        bool an = am < 0;
        bool bn = bm < 0;
        if (an) am = -am;
        if (bn) bm = -bm;
        while (bit_count(am) < 50) {
            am*=10;
            --ae;
        }
        auto res = am / bm;
        if (an != bn) res = -res;
        return FixedNumber(res, ae-be);
    }


    constexpr FixedNumber &operator+=(const FixedNumber &a) {
        (*this) = (*this) + a;
        return *this;
    }

    constexpr FixedNumber &operator-=(const FixedNumber &a) {
        (*this) = (*this) - a;
        return *this;
    }

    friend constexpr FixedNumber operator-(const FixedNumber &a) {
        return FixedNumber(-a.get_mantisa(), a.get_exponent());
    }

    friend constexpr bool operator==(const FixedNumber &a, const FixedNumber &b) {
        int ea = a.get_exponent();
        int eb = b.get_exponent();
        if (ea != eb) {
            int ecmn = std::max(ea,eb);
            return a.set_exponent(ecmn) == b.set_exponent(ecmn);
        } else {
            return a.get_mantisa() == b.get_mantisa();
        }
    }

    friend constexpr std::strong_ordering operator<=>(const FixedNumber &a, const FixedNumber &b) {
        int ea = a.get_exponent();
        int eb = b.get_exponent();
        if (ea != eb) {
            int ecmn = std::max(ea,eb);
            return a.set_exponent(ecmn) <=> b.set_exponent(ecmn);
        } else {
            auto am = a.get_mantisa();
            auto bm = b.get_mantisa();
            return am < bm? std::strong_ordering::less:am>bm?std::strong_ordering::greater:std::strong_ordering::equal;
        }
    }

    constexpr std::string_view to_sci_string(std::vector<char> &buffer) const {
        buffer.clear();
        auto e = get_exponent();
        auto m = get_mantisa();
        if (m < 0) {
            buffer.push_back('-');
            m = -m;
        }
        char tmp[24];
        char *a = tmp+sizeof(tmp);
        int digits = 0;
        while (m) {
            --a;
            *a = '0' + (m % 10);
            m/=10;
            ++digits;
        }
        if (digits == 0) {
            buffer.push_back('0');
            e = 0;
        } else if (digits == 1) {
            buffer.push_back(*a);
        } else {
            buffer.push_back(*a);
            ++a;
            buffer.push_back('.');
            for (int i=1; i < digits;++i) {
                buffer.push_back(*a);
                ++a;
            }
            e += digits-1;
        }
        buffer.push_back('e');
        digits = 0;
        if (e == 0) {
            buffer.push_back('0');
        } else {
            if (e < 0) {
                buffer.push_back('-');
                e = -e;
            }
            while (e) {
                --a;
                *a = '0' + (e % 10);
                e/=10;
                ++digits;
            }
            for (int i = 0; i < digits; ++i) {
                buffer.push_back(*a);
                ++a;
            }
        }
        return {buffer.data(), buffer.size()};
    }

    constexpr std::string_view to_fixed_string(std::vector<char> &buffer) const {
        buffer.clear();
        auto e = get_exponent();
        auto m = get_mantisa();
        if (m < 0) {
            buffer.push_back('-');
            m = -m;
        }
        char tmp[24];
        char *a = tmp+sizeof(tmp);
        int digits = 0;
        while (m) {
            --a;
            *a = '0' + (m % 10);
            m/=10;
            ++digits;
        }
        if (e >= 0) {
            for (int i = 0; i < digits; ++i) {
                buffer.push_back(a[i]);
            }
            for (int i = 0; i < e; ++i) {
                buffer.push_back('0');
            }
        } else if (e <= -digits) {
            buffer.push_back('0');
            buffer.push_back('.');
            int zrs = -e - digits;
            for (int i = 0; i < zrs; ++i) buffer.push_back('0');
            for (int i = 0; i < digits; ++i) buffer.push_back(a[i]);
        } else {
            int dn = digits + e;
            for (int i = 0; i < dn; ++i) buffer.push_back(a[i]);
            buffer.push_back('.');
            for (int i = dn; i < digits; ++i) buffer.push_back(a[i]);
        }
        return {buffer.data(), buffer.size()};
    }

    constexpr FixedNumber set_exponent(int new_exponent) const {
        auto m = get_mantisa();
        auto old_e = get_exponent();
        auto expdiff = new_exponent - old_e;
        if (expdiff < 0) {
            m = m * pow10(-expdiff);
        } else {
            m = m / pow10(expdiff);
        }
        return FixedNumber(m, new_exponent);
    }

    constexpr std::int64_t get_mantisa() const {
        return _num_data>>8;
    }
    constexpr int get_exponent() const {
        return static_cast<int>(_num_data & 0xFF)-128;
    }

protected:
    std::int64_t _num_data;


    constexpr static std::int64_t make_number(std::int64_t value, int exponent) {
        auto eadj = static_cast<std::int64_t>(exponent+128) & 0xFF;
        return (value << 8) | eadj;
    }

    constexpr static std::int64_t pow10(int exp) {
        std::int64_t r = 1;
        while (exp > 0) {
            r = r * 10;
            --exp;
        }
        return r;
    }


    constexpr static int bit_count(std::uint64_t x) {
        int n = 0;
        while (x) {
            ++n;
            x = x >> 1;
        }
        return n;
    }

    static constexpr int minexp = std::numeric_limits<double>::min_exponent10;
    static constexpr int maxexp = std::numeric_limits<double>::max_exponent10;

    static constexpr double pow_cont(double base, int exponent) {
         if (exponent == 0) return 1.0;
         if (exponent % 2 == 0) {
             double halfPower = pow_cont(base, exponent / 2);
             return halfPower * halfPower;
         } else {
             return base * pow_cont(base, exponent - 1);
         }
     }

    static constexpr double pow(double base, int exponent) {
         if (exponent == 1) return base;
         if (exponent < 0) {
             base = 1.0 / base;
             exponent = -exponent;
         }
         return pow_cont(base, exponent);
     }


    static constexpr int log10(double number)  {
        if (number > 0) {
            auto low = minexp;
            auto high = maxexp+1;
            while (low < high) {
                auto mid = (low+ high - 2*minexp)/2 + minexp;
                auto v = pow10(mid);
                auto adj = number/v;
                if (adj < 1.0) high = mid;
                else if (adj >= 10.0) low = mid+1;
                else return mid;

            }
            return low;
        } else {
            return 0;
        }
    }

    static constexpr double abs(double x) {
        return x<0?-x:x;
    }

    static constexpr std::int64_t double2raw(double v) {
        if (!v) return make_number(0, 0);
        int exponent = static_cast<int>(log10(abs(v))) - 8;
        double base = pow(10, exponent);
        double num = v/base;
        std::int64_t n = static_cast<std::int64_t>(num);
        return make_number(n, exponent);
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
                    if (m > 0x7FFFFFFFFFFFFFLL) {
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
                        if (m > 0x7FFFFFFFFFFFFFLL)  {
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

};

}
