#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <cmath>


#include <compare>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdatomic.h>
#include <stdexcept>
#include <string_view>
#include <type_traits>


#if defined(__SIZEOF_INT128__)
#  if defined(__clang__) || defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wpedantic"
#  endif
using int128_t = __int128;
#  if defined(__clang__) || defined(__GNUC__)
#    pragma GCC diagnostic pop
#  endif
#else
#  error "Compiler must support int128_t for multiply_and_div64"
#endif

namespace _decimal_details {

    inline constexpr int count_bits(uint64_t n) {
        if (n == 0) return 0;
        return 64-std::countl_zero(n);
    }

    inline constexpr auto digitCountLog2Table = ([]{
        std::array<int ,65> out = {};
        for (std::size_t i =0; i < 64; ++i) {
            auto tst = (1ULL << i)-1;
            int cnt = 0;
            while (tst) {
                tst/=10;
                cnt++;
            }
            out[i] = cnt;
        }
        out[64] = 20;
        return out;
    })();


    inline constexpr auto lowestNumberWithNDigitsTable = ([]{
        std::array<std::uint64_t,21> out = {};
        std::uint64_t acc = 1;
        for (std::size_t i = 1; i < out.size(); i++) {
            out[i] = acc;
            acc = acc * 10;
        }
        return out;
    })();
   

    inline constexpr int count_digits(uint64_t n) {
        auto  bits =count_bits(n);
        auto digits = digitCountLog2Table[static_cast<std::size_t>(bits)];
        auto thr = lowestNumberWithNDigitsTable[static_cast<std::size_t>(digits)];
        return digits - (n < thr?1:0);
    }

    template<int64_t k>
    inline constexpr std::int64_t multiply_and_div64(int64_t a, int64_t b) {
        auto m = static_cast<int128_t>(a) * static_cast<int128_t>(b);
        return static_cast<int64_t>(m/ k);
    }

    ///enforces compiler to implement division as fixed with multiplication
    inline constexpr std::int64_t divide_pow10(int64_t a, unsigned int b) {
        switch (b){
            case 0: return  a;
            case 1: return  a/10LL;
            case 2: return  a/100LL;
            case 3: return  a/1000LL;
            case 4: return  a/10000LL;
            case 5: return  a/100000LL;
            case 6: return  a/1000000LL;
            case 7: return  a/10000000LL;
            case 8: return  a/100000000LL;
            case 9: return  a/1000000000LL;
            case 10: return a/10000000000LL;
            case 11: return a/100000000000LL;
            case 12: return a/1000000000000LL;
            case 13: return a/10000000000000LL;
            case 14: return a/100000000000000LL;
            case 15: return a/1000000000000000LL;
            case 16: return a/10000000000000000LL;
            case 17: return a/100000000000000000LL;
            case 18: return a/1000000000000000000LL;
            default: throw "exponent out of range";
        }
    }

    inline constexpr std::int64_t multiply_pow10(int64_t a, unsigned int b) {
        switch (b){
            case 0: return  a;
            case 1: return  a*10LL;
            case 2: return  a*100LL;
            case 3: return  a*1000LL;
            case 4: return  a*10000LL;
            case 5: return  a*100000LL;
            case 6: return  a*1000000LL;
            case 7: return  a*10000000LL;
            case 8: return  a*100000000LL;
            case 9: return  a*1000000000LL;
            case 10: return a*10000000000LL;
            case 11: return a*100000000000LL;
            case 12: return a*1000000000000LL;
            case 13: return a*10000000000000LL;
            case 14: return a*100000000000000LL;
            case 15: return a*1000000000000000LL;
            case 16: return a*10000000000000000LL;
            case 17: return a*100000000000000000LL;
            case 18: return a*1000000000000000000LL;
            default: throw "exponent out of range";
        }
    }



}



class Decimal {
public:



    using Mantissa = std::int64_t;
    using Exponent = std::int8_t;
    static constexpr unsigned int mantissa_bits = 56;    
    static constexpr unsigned int exponent_bits = 8;
    static constexpr auto mantissa_digits = _decimal_details::digitCountLog2Table[mantissa_bits]-1;
    static constexpr auto mantissa_effective_bits = _decimal_details::count_bits(_decimal_details::lowestNumberWithNDigitsTable[mantissa_digits+1]-1);
    static constexpr auto mantissa_max = _decimal_details::lowestNumberWithNDigitsTable[mantissa_digits+1]-1;
    static constexpr auto mantissa_min = _decimal_details::lowestNumberWithNDigitsTable[mantissa_digits];
    static constexpr Exponent exponent_max = std::numeric_limits<Exponent>::max();
    static constexpr Exponent exponent_min = std::numeric_limits<Exponent>::min();


    constexpr Exponent exponent() const {
        return static_cast<std::int8_t>(_packed & 0xFF);
    }
    constexpr Mantissa mantissa() const {
        return _packed >> 8;
    }

    constexpr Decimal() = default;
    
    ///direct construct from mantisa and exponent, no normalization is performed!
    constexpr Decimal(Mantissa m, Exponent e):_packed((m << exponent_bits) | static_cast<uint8_t>(e)) {}
    
    ///construct normalized
    /**
        returns m * 10 ^ e normalized
    */
    static constexpr Decimal normalize(Mantissa m, Exponent e) {        
        if (m == 0) return Decimal();        
        auto digits = _decimal_details::count_digits(static_cast<std::uint64_t>(m<0?-m:m));
        auto shift = mantissa_digits - digits;
        if (shift < 0) {            
            return Decimal(_decimal_details::divide_pow10(m, static_cast<unsigned int>(-shift)), static_cast<Exponent>(e-shift));
        } else {
            return Decimal(_decimal_details::multiply_pow10(m,static_cast<unsigned int>(shift)), static_cast<Exponent>(e-shift));
        }
    }

    static constexpr Decimal scale(Mantissa number, Exponent e) {
        return scaleb10(Decimal(number),e);
    }

    template<typename T>
    requires(std::is_arithmetic_v<T> && std::is_integral_v<T>)    
    constexpr Decimal(T val): Decimal(normalize(static_cast<int64_t>(val),mantissa_digits)) {}
    

    Decimal(double value):Decimal(from_double(value)) {}

    static Decimal from_double(double value) {
        auto b =value < 0;
        if (b) value = -value;
        auto lg = std::ceil(std::log10(value));
        return normalize(static_cast<std::int64_t>((b?-1:1)*std::round(value * std::pow(10,-lg+mantissa_digits))), static_cast<Exponent>(lg));
    }

    constexpr explicit operator bool() const {return _packed != 0;}
    
    friend constexpr Decimal scaleb10(const Decimal &src, int exponent) {
        if (!src) return src;
        return Decimal(src.mantissa(), static_cast<Exponent>(src.exponent()+exponent));
    }

    static constexpr Decimal from_string(std::string_view literal) {
        auto iter = literal.begin();
        auto e = literal.end();
        bool neg = false;
        int exp = 0;
        Decimal accum;
        while (iter != e && *iter >= '0' && *iter < ' ') ++iter;
        if (iter != e) {
            if (*iter == '-') {neg = true;++iter;}
            else if (*iter == '+') {++iter;}            
        }

        while (iter != e) {
            if (*iter >= '0' && *iter <= '9') {
                accum = scaleb10(accum ,1) + (*iter - '0');
                ++iter;
            } else {
                break;
            }
        }

        if (iter != e && *iter == '.') {
            ++iter;
            auto scl = 0;
            while (iter != e) {
                if (*iter >= '0' && *iter <= '9') {
                    --scl;
                    accum = accum + scaleb10(Decimal(*iter - '0'),scl);
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
            accum = scaleb10(accum, exp);
        }
        if (iter != e) throw "Invalid number format";
        if (neg) accum = -accum;
        return accum;
    }
           
    constexpr double to_double() const {
        return static_cast<double>(mantissa()) * pow10c(exponent() - mantissa_digits);
    }
    
    constexpr bool operator==(const Decimal &other) const = default;

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

    static constexpr std::int64_t round_mantissa(std::int64_t m, unsigned int shift) {
        std::int64_t n = static_cast<std::int64_t>(_decimal_details::lowestNumberWithNDigitsTable[shift+1]);
        if (m < 0) {
            return _decimal_details::divide_pow10(m - n/2, shift);
        } else {
            return _decimal_details::divide_pow10(m + n/2, shift);
        }
    }

    static constexpr std::int64_t floor_mantisa(std::int64_t m, unsigned int shift) {
        if (m < 0) {
            std::int64_t n = static_cast<std::int64_t>(_decimal_details::lowestNumberWithNDigitsTable[shift]);
            auto md = m % n;            
            return _decimal_details::divide_pow10(m,shift) - (md?1:0);
        } else {
            return _decimal_details::divide_pow10(m,shift);
        }
    }

    static constexpr std::int64_t ceil_mantisa(std::int64_t m, unsigned int shift) {
        if (m < 0) {
            return _decimal_details::divide_pow10(m,shift);
        } else {
            std::int64_t n = static_cast<std::int64_t>(_decimal_details::lowestNumberWithNDigitsTable[shift]);
            auto md = m % n;            
            return _decimal_details::divide_pow10(m,shift) + (md?1:0);
        }
    }

    constexpr Decimal operator-() const {
        return Decimal(-mantissa(), exponent());
    }
    

    friend constexpr Decimal operator+(Decimal a, Decimal b) {
        auto ae = a.exponent();
        auto be = b.exponent();
        auto am = a.mantissa();
        auto bm = b.mantissa();
        if (a.exponent() > b.exponent()) {
            bm = round_mantissa(bm, static_cast<unsigned int>(ae - be));
            return normalize(am+bm, ae);
        } else {
            am =  round_mantissa(am, static_cast<unsigned int>(be - ae));
            return normalize(am+bm, be);
        }
    }

    friend constexpr Decimal operator-(Decimal a, Decimal b) {
        return a + (-b);
    }


    friend constexpr Decimal operator*(Decimal a, Decimal b) {
        auto r = _decimal_details::multiply_and_div64<(mantissa_max+1)/100>(a.mantissa(), b.mantissa());
        auto e = a.exponent()+b.exponent()-2;
        return normalize(static_cast<Mantissa>(r), static_cast<Exponent>(e));
    }

    friend constexpr Decimal reciprocal(Decimal a) {
        if (!a) throw std::runtime_error("Decimal division by zero");
        //use double calculation to calculate reciprocal mantisa
        auto tmp = Decimal::normalize(static_cast<int64_t>(static_cast<double>(mantissa_max+1)/static_cast<double>(a.mantissa())*(mantissa_max+1)),0);
        return scaleb10(tmp, -a.exponent()); //adjust exponent
    }

    friend constexpr Decimal operator/(Decimal a, Decimal b) {
        return a * reciprocal(b);
    }

    Decimal &operator+= (Decimal other) {*this = *this + other;return *this;}
    Decimal &operator-= (Decimal other) {*this = *this - other;return *this;}
    Decimal &operator*= (Decimal other) {*this = *this * other;return *this;}
    Decimal &operator/= (Decimal other) {*this = *this / other;return *this;}

    friend constexpr int sgn(const Decimal &other) {
        return other._packed<0?-1:other._packed>0?1:0;
    }

    friend constexpr Decimal abs(const Decimal &other) {
        if (other._packed<0) return -(other);
        else return other;
    }

    constexpr int64_t compare(const Decimal &other) const {
        auto s1 = sgn(*this);
        auto s2 = sgn(other);
        if (s1 != s2) return s1-s2;
        auto d = exponent() - other.exponent();
        if (!d) return mantissa() - other.mantissa();
        return d;
    }
    

    constexpr std::strong_ordering operator<=>(const Decimal &other) const {
        auto c = compare(other);
        if (c < 0) return std::strong_ordering::less;
        if (c > 0)return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }

    friend constexpr Decimal round(const Decimal &other) {
        auto e = other.exponent();
        if (e >= mantissa_digits) return other;
        auto r =round_mantissa(other.mantissa(), static_cast<unsigned int>(-e+mantissa_digits));
        return Decimal(r);
    }

    friend constexpr Decimal floor(const Decimal &other) {
        auto e = other.exponent();
        if (e >= mantissa_digits) return other;
        auto r =floor_mantisa(other.mantissa(),  static_cast<unsigned int>(-e+mantissa_digits));
        return Decimal(r);
    }

    friend constexpr Decimal ceil(const Decimal &other) {
        auto e = other.exponent();
        if (e >= mantissa_digits) return other;
        auto r =ceil_mantisa(other.mantissa(),  static_cast<unsigned int>(-e+mantissa_digits));
        return Decimal(r);
    }


  
    template<typename _Out>
    constexpr _Out to_string_fixed(_Out out, int decimals) const {
        Decimal v;


        if (sgn(*this) < 0) {
            *out++='-';
            v = -*this;
        } else {
            v = *this;
        }

        if (decimals == 0) {
            v = round(v);
        }

        
        Decimal f = floor(v);
        Decimal d = round(scaleb10(v - f,decimals));

        auto fe = f.exponent();

        int maxdigits = std::min<int>(fe, mantissa_digits);

        auto m =_decimal_details::divide_pow10(f.mantissa(),static_cast<unsigned int>(mantissa_digits - maxdigits));
        out = out_number(out, m);
        int remain = fe - maxdigits;
        for (int i = 0; i < remain;++i) *out++ = '0';

        if (decimals) {
            remain = decimals;
            auto dm = d.mantissa();
            *out++ = '.';
            if (dm != 0) {
                maxdigits = std::min(decimals, mantissa_digits);
                m = _decimal_details::divide_pow10(dm,static_cast<unsigned int>(mantissa_digits - maxdigits));
                out = out_number(out, m);
                remain = decimals - mantissa_digits;
            }
            for (int i = 0; i < remain;++i) *out++ = '0';
        }
        return out;
    }

    constexpr std::string to_string_fixed(int decimals) const {
        std::string out;
        to_string_fixed(std::back_inserter(out), decimals);
        return out;
    }


    template<typename _Out>
    constexpr _Out to_string_sci(_Out out, int decimals) const {
        auto exp = exponent();
        Decimal adj = scaleb10(*this, -exp+1);
        out = adj.to_string_fixed(out, decimals);
        *out++='E';
        out = Decimal(exp-1).to_string_fixed(out, 0);
        return out;
    }

    constexpr std::string to_string_sci(int decimals) const {
        std::string out;
        to_string_sci(std::back_inserter(out), decimals);
        return out;
    }

    template<typename _Out>
    constexpr _Out to_string(_Out out) const {
        char buff[mantissa_digits*2+5];
        char *ptr;

        auto remove_trailing = [&]{
            while (ptr > buff && *(ptr-1) == '0') {
                --ptr;
            }
            if (ptr > buff && *(ptr-1) == '.') {
                --ptr;
            }
        };

        auto exp = exponent();
        if (exp > mantissa_digits || exp < -2) {
            Decimal adj = scaleb10(*this, -exp+1);
            ptr = adj.to_string_fixed(buff, mantissa_digits);
            remove_trailing();
            *ptr++='E';
            ptr = Decimal(exp-1).to_string_fixed(ptr, 0);            
        } else {
            ptr = this->to_string_fixed(buff, mantissa_digits);
            remove_trailing();            
        }
        return std::copy(std::begin(buff), ptr, out);
    }


    std::string to_string() const {
        std::string buff;
        to_string(std::back_inserter(buff));
        return buff;
    }

    static constexpr Decimal max() {
        return Decimal(mantissa_max, exponent_max);
    }
    static constexpr Decimal min() {
        return Decimal(mantissa_min, exponent_min);
    }

    template<typename IOStream>
    friend IOStream &operator<<(IOStream &stream, Decimal dec) {
        auto iter =   std::ostreambuf_iterator<char>(stream);
        dec.to_string(iter);
        return stream;
    }
  
    protected:

        std::int64_t _packed = 0;
 
        template<typename Iter>
        static constexpr Iter out_number2(Iter iter, std::int64_t number) {
            if (number) {
                int z = static_cast<int>(number % 10);
                iter = out_number2(iter, number/10);
                *iter++ = static_cast<char>(z + '0');
            }
            return iter;
        }


        template<typename Iter>
        static constexpr Iter out_number(Iter iter, std::int64_t number) {
            if (number) return out_number2(iter, number);
            else {
                *iter++='0';
                return iter;
            }
        }


};

constexpr Decimal operator "" _dec(const char* k){
      return Decimal::from_string(k);
}