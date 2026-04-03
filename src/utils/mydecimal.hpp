#pragma once
#include <cmath>


#include <compare>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <type_traits>
class MyDecimal {
public:

    static constexpr int manttisa_decimals = 9;

    constexpr MyDecimal()  = default;
    constexpr MyDecimal(int32_t m, int32_t e)  :_m(m),_e(e) {}
    template<typename T>
    requires(std::is_arithmetic_v<T> && std::is_integral_v<T>)    
    constexpr MyDecimal(T val): MyDecimal(normalize(static_cast<int64_t>(val),9)) {}
    MyDecimal(double value):MyDecimal(from_double(value)) {}
    static MyDecimal from_double(double value) {
        auto b = std::signbit(value);
        if (b) value = -value;
        auto lg = std::ceil(std::log10(value));
        return MyDecimal::normalize(static_cast<std::int64_t>(std::round(value * std::pow(10,-lg+manttisa_decimals))), static_cast<std::int32_t>(lg));
    }

    static constexpr MyDecimal from_string(std::string_view literal) {
        auto iter = literal.begin();
        auto e = literal.end();
        bool neg = false;
        int exp = 0;
        MyDecimal accum;
        while (iter != e && *iter >= '0' && *iter < ' ') ++iter;
        if (iter != e) {
            if (*iter == '-') {neg = true;++iter;}
            else if (*iter == '+') {++iter;}            
        }

        while (iter != e) {
            if (*iter >= '0' && *iter <= '9') {
                accum = accum * 10 + (*iter - '0');
                ++iter;
            } else {
                break;
            }
        }

        if (iter != e && *iter == '.') {
            ++iter;
            auto mult = MyDecimal(1);
            while (iter != e) {
                if (*iter >= '0' && *iter <= '9') {
                    mult = mult / MyDecimal(10);
                    accum = accum + mult * (*iter - '0');                                    
                    ++iter;
                } else {
                    break;
                }
            }
        }
        if (iter != e && (*iter == 'e' || *iter == 'E')) {
            int s = 1;
            ++iter;
            if (iter != e) {
                if (*iter == '+') {++iter;}
                else if (*iter == '-') {s = -1; ++iter;};
            }
            while (iter != e) {
                if (*iter >= '0' && *iter <= '9') {
                    exp = exp * 10 + (*iter - '0');
                    ++iter;
                } else {
                    break;
                }
            }        
            exp = s * exp;
            accum = accum  * MyDecimal(100000000, exp);
        }
        if (iter != e) throw "Invalid number format";
        if (neg) accum = -accum;
        return accum;
    }
        
    constexpr auto manttisa() const {return _m;}
    constexpr auto exponent() const {return _e;}
    
    constexpr double to_double() const {
        return _m * pow10c(_e - manttisa_decimals);
    }
    
    constexpr bool operator==(const MyDecimal &other) const = default;

    static constexpr int calc_normalize(std::int64_t x) {
        if (x < 0) x = -x;
        if (x < 1LL) return 9;
        if (x < 10LL) return 8;
        if (x < 100LL) return 7;
        if (x < 1000LL) return 6;
        if (x < 10000LL) return 5;
        if (x < 100000LL) return 4;
        if (x < 1000000LL) return 3;
        if (x < 10000000LL) return 2;
        if (x < 100000000LL) return 1;
        if (x < 1000000000LL) return 0;
        if (x < 10000000000LL) return -1;
        if (x < 100000000000LL) return -2;
        if (x < 1000000000000LL) return -3;
        if (x < 10000000000000LL) return -4;
        if (x < 100000000000000LL) return -5;
        if (x < 1000000000000000LL) return -6;
        if (x < 10000000000000000LL) return -7;
        if (x < 100000000000000000LL) return -8;
        if (x < 1000000000000000000LL) return -9;
        return -10;
    }

    static constexpr int32_t pow10table [] = {
            1LL, 10LL, 100LL, 1000LL, 10000LL, 100000LL, 1000000LL, 10000000LL, 100000000LL, 1000000000LL
    };

    static constexpr MyDecimal normalize(int64_t manttisa, int32_t exponent) {
        auto adj = calc_normalize(manttisa);
        if (adj < 0) {
            auto rounded = manttisa + (manttisa<0?-1:1) * static_cast<int64_t>(pow10table[-adj])/2;
            adj = calc_normalize(rounded);
            return MyDecimal(static_cast<int32_t>(rounded / static_cast<int64_t>(pow10table[-adj])), exponent-adj);
        } 
        if (adj > 0) {
            return MyDecimal(static_cast<int32_t>(manttisa * pow10table[adj]), exponent-adj);
        }
        return MyDecimal(static_cast<int32_t>(manttisa), exponent);
    }

    static constexpr int32_t round_manttisa(int32_t manttisa, uint32_t exp) {
        if (exp > 9) return 0;
        auto rounded = manttisa + (manttisa<0?-1:1) * (pow10table[exp]/2);
        return rounded / pow10table[exp];
    }

    static constexpr double pow10c(int32_t exp) {
        
        if (std::is_constant_evaluated()) {
            double r= 1.0;
            while (exp<0) {
                r = r / 10;
                exp++;
            }
            while (exp>0) {
                r = r * 10;
                exp--;
            }
            return r;
        } else {
            return std::pow(10.0, static_cast<double>(exp));
        }
    }

    constexpr MyDecimal operator-() const {
        return MyDecimal(-_m, _e);
    }

    friend constexpr MyDecimal operator+(MyDecimal a, MyDecimal b) {
        if (a.exponent() > b.exponent()) {
            auto m1 = a._m;
            auto m2 = round_manttisa(b._m, a.exponent() - b.exponent());
            return normalize(m1+m2, a._e);
        } else {
            auto m1 =  round_manttisa(a._m, b.exponent() - a.exponent());
            auto m2 =b ._m;
            return normalize(m1+m2, b._e);
        }
    }

    friend constexpr MyDecimal operator-(MyDecimal a, MyDecimal b) {
        return a + (-b);
    }

    friend constexpr MyDecimal operator*(MyDecimal a, MyDecimal b) {
        auto m = static_cast<int64_t>(a._m) * static_cast<int64_t>(b._m);
        auto e = a._e + b._e - manttisa_decimals;
        return MyDecimal::normalize(m,e);
    }

    friend constexpr MyDecimal operator/(MyDecimal a, MyDecimal b) {
        auto m = static_cast<int64_t>(a._m) * 1000000000LL / static_cast<int64_t>(b._m);
        auto e = a._e - b._e;
        return MyDecimal::normalize(m,e);
    }

    friend constexpr int sgn(const MyDecimal &other) {
        return other._m<0?-1:other._m>0?1:0;
    }

    constexpr int32_t compare(const MyDecimal &other) const {
        auto s1 = sgn(*this);
        auto s2 = sgn(other);
        if (s1 != s2) return s1-s2;
        auto d = _e - other._e;
        if (!d) return _m - other._m;
        return d;
    }

    constexpr std::strong_ordering operator<=>(const MyDecimal &other) const {
        auto c = compare(other);
        if (c < 0) return std::strong_ordering::less;
        if (c > 0)return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }

    friend constexpr MyDecimal round(const MyDecimal &other) {
        if (other._e >= manttisa_decimals) return other;
        auto r =round_manttisa(other._m, -other._e+manttisa_decimals);
        return MyDecimal(r);
    }

    friend constexpr MyDecimal floor(const MyDecimal &other) {
        if (other._e >= manttisa_decimals) return other;
        return round(other - MyDecimal(500000000,0));
    }

    friend constexpr MyDecimal ceil(const MyDecimal &other) {
        if (other._e >= manttisa_decimals) return other;
        return round(other + MyDecimal(499999999,0));
    }


    ///fast calculates val * pow10(e)
    constexpr MyDecimal pow10(int e) const {
        return MyDecimal(_m, _e+e);
    }
    

    template<typename _Out>
    constexpr _Out to_string_fixed(_Out out, int decimals) {
        MyDecimal v;

        if (sgn(*this) < 0) {
            *out++='-';
            v = -*this;
        } else {
            v = *this;
        }

        if (decimals == 0) {
            v = round(v);
        }

        
        MyDecimal f = floor(v);
        MyDecimal d = round((v - f).pow10(decimals));

        int maxdigits = std::min(f._e, manttisa_decimals);

        auto m =f._m/pow10table[manttisa_decimals - maxdigits];
        out = out_number(out, m);
        int remain = f._e - maxdigits;
        for (int i = 0; i < remain;++i) *out++ = '0';

        if (decimals) {
            remain = decimals;
            *out++ = '.';
            if (d._m != 0) {
                maxdigits = std::min(decimals, manttisa_decimals);
                m = d._m/pow10table[manttisa_decimals - maxdigits];
                out = out_number(out, m);
                remain = decimals - manttisa_decimals;
            }
            for (int i = 0; i < remain;++i) *out++ = '0';
        }
        return out;
    }

  
    protected:
        std::int32_t _m = 0;
        std::int32_t _e = 0;

        template<typename Iter>
        static constexpr Iter out_number2(Iter iter, int32_t number) {
            if (number) {
                int z = number % 10;
                iter = out_number2(iter, number/10);
                *iter++ = z + '0';                
            }
            return iter;
        }


        template<typename Iter>
        static constexpr Iter out_number(Iter iter, int32_t number) {
            if (number) return out_number2(iter, number);
            else {
                *iter++='0';
                return iter;
            }
        }


};

constexpr MyDecimal operator "" _dec(const char* k){
      return MyDecimal::from_string(k);
}