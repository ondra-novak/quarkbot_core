#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <algorithm>



class Decimal {
public:
    constexpr Decimal() = default;
    constexpr Decimal(int64_t value, uint32_t precision = 0);
    constexpr Decimal(int value, uint32_t precision = 0) : Decimal(static_cast<int64_t>(value), precision) {}
    constexpr Decimal(double d, uint32_t precision);

    constexpr int64_t scaled_value() const noexcept { return m_value; }
    constexpr uint32_t precision()    const noexcept { return m_precision; }
    constexpr double  to_double()    const noexcept {
        return static_cast<double>(m_value) / static_cast<double>(pow10(m_precision));
    }

    constexpr Decimal operator+(const Decimal& rhs) const;
    constexpr Decimal operator-(const Decimal& rhs) const;
    constexpr Decimal& operator+=(const Decimal& rhs);
    constexpr Decimal& operator-=(const Decimal& rhs);

    constexpr Decimal operator*(const Decimal& rhs) const;
    constexpr Decimal& operator*=(const Decimal& rhs);

    constexpr Decimal operator/(const Decimal& rhs) const;
    constexpr Decimal& operator/=(const Decimal& rhs);

    constexpr bool operator==(const Decimal& rhs) const;
    constexpr bool operator!=(const Decimal& rhs) const;
    constexpr bool operator< (const Decimal& rhs) const;
    constexpr bool operator<=(const Decimal& rhs) const;
    constexpr bool operator> (const Decimal& rhs) const;
    constexpr bool operator>=(const Decimal& rhs) const;

    constexpr Decimal round_to(uint32_t new_precision) const;
    constexpr static Decimal min() {return Decimal{};}
    constexpr static Decimal max() {return Decimal{std::numeric_limits<int64_t>::max(),0};}

private:
    int64_t  m_value = 0;
    uint32_t  m_precision = 0;

    constexpr static int64_t pow10(uint32_t n);
    constexpr Decimal scale_to(uint32_t target_precision) const;
    static constexpr int64_t table[] = {
        1LL,
        10LL,
        100LL,
        1'000LL,
        10'000LL,
        100'000LL,
        1'000'000LL,
        10'000'000LL,
        100'000'000LL,
        1'000'000'000LL,
        10'000'000'000LL,
        100'000'000'000LL,
        1'000'000'000'000LL,
        10'000'000'000'000LL,
        100'000'000'000'000LL,
        1'000'000'000'000'000LL,
        10'000'000'000'000'000LL,
        100'000'000'000'000'000LL,
        1'000'000'000'000'000'000LL,
    };

};

// ── Constructors ─────────────────────────────────────────────────────────────

inline constexpr Decimal::Decimal(int64_t value, uint32_t precision)
    : m_value(value), m_precision(precision)
{
}

inline constexpr Decimal::Decimal(double d, uint32_t precision)
    : m_precision(precision)
{
    if (precision < 0)
        throw std::invalid_argument("precision must be >= 0");
    double scaled = d * static_cast<double>(pow10(precision));
    m_value = static_cast<int64_t>(scaled < 0.0 ? scaled - 0.5 : scaled + 0.5);
}

// ── Internal helpers ─────────────────────────────────────────────────────────

inline constexpr int64_t Decimal::pow10(uint32_t n)
{
    if (n < 0 || n > 18)
        throw std::overflow_error("precision out of supported range [0, 18]");
    return table[n];
}

inline constexpr Decimal Decimal::scale_to(uint32_t target) const
{
    if (target < m_precision)
        throw std::invalid_argument("scale_to: target must be >= current precision");
    if (target == m_precision) return *this;
    int64_t factor = pow10(target - m_precision);
    __int128 result = static_cast<__int128>(m_value) * factor;
    if (result > INT64_MAX || result < INT64_MIN)
        throw std::overflow_error("scale_to: value overflows int64_t");
    return Decimal(static_cast<int64_t>(result), target);
}

// ── Arithmetic ───────────────────────────────────────────────────────────────

inline constexpr Decimal Decimal::operator+(const Decimal& rhs) const
{
    uint32_t max_p = std::max(m_precision, rhs.m_precision);
    auto a = scale_to(max_p);
    auto b = rhs.scale_to(max_p);
    __int128 result = static_cast<__int128>(a.m_value) + b.m_value;
    if (result > INT64_MAX || result < INT64_MIN)
        throw std::overflow_error("addition overflow");
    return Decimal(static_cast<int64_t>(result), max_p);
}

inline constexpr Decimal Decimal::operator-(const Decimal& rhs) const
{
    uint32_t max_p = std::max(m_precision, rhs.m_precision);
    auto a = scale_to(max_p);
    auto b = rhs.scale_to(max_p);
    __int128 result = static_cast<__int128>(a.m_value) - b.m_value;
    if (result > INT64_MAX || result < INT64_MIN)
        throw std::overflow_error("subtraction overflow");
    return Decimal(static_cast<int64_t>(result), max_p);
}

inline constexpr Decimal& Decimal::operator+=(const Decimal& rhs) { *this = *this + rhs; return *this; }
inline constexpr Decimal& Decimal::operator-=(const Decimal& rhs) { *this = *this - rhs; return *this; }

inline constexpr Decimal Decimal::operator*(const Decimal& rhs) const
{
    uint32_t new_precision = m_precision + rhs.m_precision;
    if (new_precision > 18)
        throw std::overflow_error("multiplication: result precision exceeds maximum (18)");
    __int128 result = static_cast<__int128>(m_value) * rhs.m_value;
    if (result > INT64_MAX || result < INT64_MIN)
        throw std::overflow_error("multiplication overflow");
    return Decimal(static_cast<int64_t>(result), new_precision);
}

inline constexpr Decimal& Decimal::operator*=(const Decimal& rhs) { *this = *this * rhs; return *this; }

inline constexpr Decimal Decimal::operator/(const Decimal& rhs) const
{
    if (rhs.m_value == 0)
        throw std::domain_error("division by zero");
    // actual(a) / actual(b) with result precision = m_precision:
    //   result_scaled = (a.m_value * 10^b.m_precision) / b.m_value
    __int128 numerator = static_cast<__int128>(m_value) * pow10(rhs.m_precision);
    __int128 result = numerator / rhs.m_value;
    if (result > INT64_MAX || result < INT64_MIN)
        throw std::overflow_error("division result overflows int64_t");
    return Decimal(static_cast<int64_t>(result), m_precision);
}

inline constexpr Decimal& Decimal::operator/=(const Decimal& rhs) { *this = *this / rhs; return *this; }

// ── Comparisons ──────────────────────────────────────────────────────────────

inline constexpr bool Decimal::operator==(const Decimal& rhs) const
{
    uint32_t max_p = std::max(m_precision, rhs.m_precision);
    return scale_to(max_p).m_value == rhs.scale_to(max_p).m_value;
}
inline constexpr bool Decimal::operator!=(const Decimal& rhs) const { return !(*this == rhs); }
inline constexpr bool Decimal::operator< (const Decimal& rhs) const
{
    uint32_t max_p = std::max(m_precision, rhs.m_precision);
    return scale_to(max_p).m_value < rhs.scale_to(max_p).m_value;
}
inline constexpr bool Decimal::operator<=(const Decimal& rhs) const { return !(rhs < *this); }
inline constexpr bool Decimal::operator> (const Decimal& rhs) const { return rhs < *this; }
inline constexpr bool Decimal::operator>=(const Decimal& rhs) const { return !(*this < rhs); }

// ── Utility ──────────────────────────────────────────────────────────────────

inline constexpr Decimal Decimal::round_to(uint32_t new_precision) const
{
    if (new_precision >= m_precision)
        return scale_to(new_precision);   // scale up — no rounding needed
    uint32_t diff    = m_precision - new_precision;
    int64_t factor  = pow10(diff);
    // Round half up (toward +infinity): floor(m_value / factor + 0.5)
    // = floor((m_value + factor/2) / factor)  using integer floor-division
    // Use __int128 for the intermediate sum — m_value near INT64_MAX can overflow int64_t
    __int128 numerator = static_cast<__int128>(m_value) + factor / 2;
    int64_t new_value;
    if (numerator < 0 && numerator % factor != 0)
        new_value = static_cast<int64_t>(numerator / factor - 1);  // floor for negative
    else
        new_value = static_cast<int64_t>(numerator / factor);
    return Decimal(new_value, new_precision);
}


