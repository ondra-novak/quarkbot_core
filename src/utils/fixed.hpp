#pragma once


#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>

class FixedBase {
public:
    constexpr static  std::array<double, 32> generate_mult_table(double f) {
        std::array<double, 32> r;        
        double b = 1;
        for (int i = 0; i < 32; ++i) {
            r[i] = b; b *= f;
        }
        return r;
    }
};


class Fixed : public FixedBase {
public:

    constexpr Fixed()  = default;

    template<unsigned int N>
    static Fixed init(double val = 0.0) {
        static_assert(N < 32);
        return Fixed(val, N);
    }

    constexpr Fixed(double val, const Fixed &template_number):_number(compose_d(val, template_number.scale())) {};
    constexpr Fixed &operator=(double v) {
        _number = compose_d(v, scale());
        return *this;
    }

    constexpr operator double() const {return static_cast<double>(raw()) * table_div[scale()];}

    constexpr std::int64_t raw() const {return _number >> 5;}
    constexpr unsigned int scale() const {return _number & 0x1F;};

    template<std::input_or_output_iterator Out>
    constexpr void to_string(Out iter) const {
        auto r = raw();
        if (r < 0) {
            *iter++ = '-';
            r = -r;
        }
        if (r == 0) {
            *iter++ = '0';
        }
        build_string(r,scale(), iter);
    }

    constexpr std::string to_string() const  {
        std::string out;
        to_string(std::back_inserter(out));
        return out;
    }

    constexpr Fixed operator+(const Fixed &other) const {
        auto cm = std::max(scale(), other.scale());
        return Fixed(static_cast<double>(*this) + static_cast<double>(other), cm);
    }
    constexpr Fixed operator-(const Fixed &other) const {
        auto cm = std::max(scale(), other.scale());
        return Fixed(static_cast<double>(*this) - static_cast<double>(other), cm);
    }
    constexpr Fixed operator*(const Fixed &other) const {
        auto cm = std::max(scale(), other.scale());
        return Fixed(static_cast<double>(*this) * static_cast<double>(other), cm);
    }
    constexpr Fixed operator/(const Fixed &other) const {
        auto cm = std::max(scale(), other.scale());
        return Fixed(static_cast<double>(*this) / static_cast<double>(other), cm);
    }

    constexpr Fixed &operator+=(double v) {
        return this->operator=(static_cast<double>(*this)+v);
    }
    constexpr Fixed &operator-=(double v) {
        return this->operator=(static_cast<double>(*this)-v);
    }
    constexpr Fixed &operator*=(double v) {
        return this->operator=(static_cast<double>(*this)*v);
    }
    constexpr Fixed &operator/=(double v) {
        return this->operator=(static_cast<double>(*this)/v);
    }
    constexpr friend int compare(Fixed a, Fixed b) {
        auto ra = a.raw();
        auto rb = b.raw();
        auto sa = a.scale();
        auto sb = b.scale();
        while (sa<sb) {
            ++sa;
            ra = ra * 10;
        }
        while (sb>sa) {
            ++sb;
            rb = rb * 10;
        }
        return ra < rb?-1:ra>rb?1:0;
    }
    constexpr Fixed operator-() const {
        return Fixed(-raw(), scale());
    }

    constexpr bool operator==(const Fixed &other) const {return compare(*this, other) == 0;}
    constexpr bool operator>(const Fixed &other) const {return compare(*this, other) > 0;}
    constexpr bool operator<(const Fixed &other) const {return compare(*this, other) < 0;}
    constexpr bool operator>=(const Fixed &other) const {return compare(*this, other) >= 0;}
    constexpr bool operator<=(const Fixed &other) const {return compare(*this, other) <= 0;}
    constexpr friend int sgn(const Fixed &f) {return f._number < 0?-1:f._number>0?1:0;}
    constexpr friend Fixed abs(const Fixed &f) {return Fixed(f.raw()<0?-f.raw():f.raw(), f.scale());}

protected:
    constexpr Fixed(double val, unsigned int scale):_number(compose_d(val, scale)) {};

    static constexpr auto table_mult = generate_mult_table(10.0);
    static constexpr auto table_div = generate_mult_table(0.1);

    std::int64_t _number = {};

    static constexpr  std::int64_t compose(std::int64_t n, unsigned int scale) {
        return (n << 5) | (scale & 0x1F);
    }

    static constexpr std::int64_t compose_d(double val, unsigned int scale) {
        auto v = static_cast<int64_t>(std::round(val * table_mult[scale]));
        return compose(v, scale);
    }
    template<std::input_or_output_iterator Out>
    static constexpr void build_string(std::uint64_t val, int scale, Out iter) {
        if (val || scale) {
            build_string(val/10, scale-1, iter);
            if (scale == 1) *iter++ = '.';
            auto n = val % 10;
            *iter++ = static_cast<char>(n+'0');            
        }
    }

    

    
};