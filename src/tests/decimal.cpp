#include "../utils/decimal.hpp"
#include "check.h"
#include <stdexcept>

// Each function encodes one SECTION from decimal.cpp.
// Exception tests (REQUIRE_THROWS_AS) are omitted — throwing in constexpr
// context causes a compile error rather than a catchable exception, so they
// remain runtime-only in decimal.cpp.

// ── Constructors and accessors ───────────────────────────────────────────────

constexpr bool test_construction_scaled_value() {
    Decimal d(123, 2);
    return d.scaled_value() == 123 && d.precision() == 2;
}

constexpr bool test_construction_to_double() {
    Decimal d(123, 2);
    return d.to_double() == 1.23;
}

constexpr bool test_construction_from_double() {
    Decimal d(1.23456, 3);
    return d.scaled_value() == 1235 && d.precision() == 3;
}

constexpr bool test_construction_negative() {
    Decimal d(-456, 2);
    return d.scaled_value() == -456 && d.to_double() == -4.56;
}

constexpr bool test_construction_zero_precision() {
    Decimal d(42, 0);
    return d.scaled_value() == 42 && d.to_double() == 42.0;
}

// ── Addition ─────────────────────────────────────────────────────────────────

constexpr bool test_add_same_precision() {
    Decimal a(100, 2), b(200, 2);
    auto r = a + b;
    return r.scaled_value() == 300 && r.precision() == 2;
}

constexpr bool test_add_different_precision() {
    Decimal a(100, 2), b(5, 1);       // 1.00 + 0.5
    auto r = a + b;
    return r.scaled_value() == 150 && r.precision() == 2;
}

constexpr bool test_add_negative_addend() {
    Decimal a(100, 2), b(-50, 2);     // 1.00 + (-0.50)
    return (a + b).scaled_value() == 50;
}

constexpr bool test_add_compound_assignment() {
    Decimal a(100, 2);
    a += Decimal(200, 2);
    return a.scaled_value() == 300;
}

// ── Subtraction ──────────────────────────────────────────────────────────────

constexpr bool test_sub_same_precision() {
    Decimal a(300, 2), b(100, 2);
    auto r = a - b;
    return r.scaled_value() == 200 && r.precision() == 2;
}

constexpr bool test_sub_different_precision() {
    Decimal a(150, 2), b(5, 1);       // 1.50 - 0.5
    auto r = a - b;
    return r.scaled_value() == 100 && r.precision() == 2;
}

constexpr bool test_sub_compound_assignment() {
    Decimal a(300, 2);
    a -= Decimal(100, 2);
    return a.scaled_value() == 200;
}

// ── Multiplication ───────────────────────────────────────────────────────────

constexpr bool test_mul_precision_accumulates() {
    Decimal a(150, 2), b(200, 2);     // 1.50 * 2.00
    auto r = a * b;
    return r.scaled_value() == 30000 && r.precision() == 4 && r.to_double() == 3.0;
}

constexpr bool test_mul_zero_precision_factor() {
    Decimal a(150, 2), b(3, 0);       // 1.50 * 3
    auto r = a * b;
    return r.scaled_value() == 450 && r.precision() == 2;
}

constexpr bool test_mul_negative_factor() {
    Decimal a(200, 2), b(-3, 0);      // 2.00 * -3
    return (a * b).scaled_value() == -600;
}

constexpr bool test_mul_compound_assignment() {
    Decimal a(150, 2);
    a *= Decimal(2, 0);
    return a.scaled_value() == 300;
}

// ── Division ─────────────────────────────────────────────────────────────────

constexpr bool test_div_exact_same_precision() {
    Decimal a(300, 2), b(300, 2);     // 3.00 / 3.00 = 1.00
    auto r = a / b;
    return r.scaled_value() == 100 && r.precision() == 2;
}

constexpr bool test_div_truncates_toward_zero() {
    Decimal a(100, 2), b(300, 2);     // 1.00 / 3.00 = 0.33
    auto r = a / b;
    return r.scaled_value() == 33 && r.precision() == 2;
}

constexpr bool test_div_different_precisions() {
    Decimal a(100, 2), b(4, 0);       // 1.00 / 4 = 0.25
    auto r = a / b;
    return r.scaled_value() == 25 && r.precision() == 2;
}

constexpr bool test_div_result_precision_follows_lhs() {
    Decimal a(1000, 3), b(3, 0);      // 1.000 / 3 = 0.333
    auto r = a / b;
    return r.scaled_value() == 333 && r.precision() == 3;
}

constexpr bool test_div_negative_dividend() {
    Decimal a(-100, 2), b(300, 2);    // -1.00 / 3.00 = -0.33
    return (a / b).scaled_value() == -33;
}

constexpr bool test_div_compound_assignment() {
    Decimal a(300, 2);
    a /= Decimal(3, 0);
    return a.scaled_value() == 100;
}

// ── Comparisons ──────────────────────────────────────────────────────────────

constexpr bool test_cmp_equal_same_precision() {
    return Decimal(100, 2) == Decimal(100, 2)
        && !(Decimal(100, 2) != Decimal(100, 2));
}

constexpr bool test_cmp_equal_different_precision() {
    return Decimal(100, 2) == Decimal(10, 1)   // 1.00 == 1.0
        && Decimal(100, 2) == Decimal(1, 0);   // 1.00 == 1
}

constexpr bool test_cmp_less_than_same_precision() {
    return Decimal(99, 2) < Decimal(100, 2)
        && !(Decimal(100, 2) < Decimal(100, 2));
}

constexpr bool test_cmp_less_than_different_precision() {
    return Decimal(9, 1) < Decimal(100, 2)         // 0.9 < 1.00
        && !(Decimal(10, 1) < Decimal(100, 2));    // 1.0 == 1.00
}

constexpr bool test_cmp_greater_than() {
    return Decimal(101, 2) > Decimal(100, 2)
        && !(Decimal(100, 2) > Decimal(100, 2));
}

constexpr bool test_cmp_less_than_or_equal() {
    return Decimal(100, 2) <= Decimal(100, 2)
        && Decimal(99, 2) <= Decimal(100, 2)
        && !(Decimal(101, 2) <= Decimal(100, 2));
}

constexpr bool test_cmp_greater_than_or_equal() {
    return Decimal(100, 2) >= Decimal(100, 2)
        && Decimal(101, 2) >= Decimal(100, 2)
        && !(Decimal(99, 2) >= Decimal(100, 2));
}

constexpr bool test_cmp_negative() {
    return Decimal(-100, 2) < Decimal(100, 2)
        && Decimal(-200, 2) < Decimal(-100, 2)
        && Decimal(-100, 2) == Decimal(-1, 0);
}

// ── round_to ─────────────────────────────────────────────────────────────────

constexpr bool test_round_half_up_positive() {
    auto r = Decimal(115, 2).round_to(1);   // 1.15 -> 1.2
    return r.scaled_value() == 12 && r.precision() == 1;
}

constexpr bool test_round_down_positive() {
    auto r = Decimal(114, 2).round_to(1);   // 1.14 -> 1.1
    return r.scaled_value() == 11;
}

constexpr bool test_round_half_up_negative() {
    auto r = Decimal(-115, 2).round_to(1);  // -1.15 -> -1.1 (toward +inf)
    return r.scaled_value() == -11;
}

constexpr bool test_round_down_negative() {
    auto r = Decimal(-114, 2).round_to(1);  // -1.14 -> -1.1 (nearest)
    return r.scaled_value() == -11;
}

constexpr bool test_round_same_precision() {
    auto r = Decimal(123, 2).round_to(2);
    return r.scaled_value() == 123 && r.precision() == 2;
}

constexpr bool test_round_higher_precision_scales_up() {
    auto r = Decimal(123, 2).round_to(4);
    return r.scaled_value() == 12300 && r.precision() == 4;
}

constexpr bool test_round_to_zero_precision() {
    auto r = Decimal(150, 2).round_to(0);   // 1.50 -> 2
    return r.scaled_value() == 2 && r.precision() == 0;
}

// ── static_assert invocations ────────────────────────────────────────────────

static_assert(test_construction_scaled_value());
static_assert(test_construction_to_double());
static_assert(test_construction_from_double());
static_assert(test_construction_negative());
static_assert(test_construction_zero_precision());

static_assert(test_add_same_precision());
static_assert(test_add_different_precision());
static_assert(test_add_negative_addend());
static_assert(test_add_compound_assignment());

static_assert(test_sub_same_precision());
static_assert(test_sub_different_precision());
static_assert(test_sub_compound_assignment());

static_assert(test_mul_precision_accumulates());
static_assert(test_mul_zero_precision_factor());
static_assert(test_mul_negative_factor());
static_assert(test_mul_compound_assignment());

static_assert(test_div_exact_same_precision());
static_assert(test_div_truncates_toward_zero());
static_assert(test_div_different_precisions());
static_assert(test_div_result_precision_follows_lhs());
static_assert(test_div_negative_dividend());
static_assert(test_div_compound_assignment());

static_assert(test_cmp_equal_same_precision());
static_assert(test_cmp_equal_different_precision());
static_assert(test_cmp_less_than_same_precision());
static_assert(test_cmp_less_than_different_precision());
static_assert(test_cmp_greater_than());
static_assert(test_cmp_less_than_or_equal());
static_assert(test_cmp_greater_than_or_equal());
static_assert(test_cmp_negative());

static_assert(test_round_half_up_positive());
static_assert(test_round_down_positive());
static_assert(test_round_half_up_negative());
static_assert(test_round_down_negative());
static_assert(test_round_same_precision());
static_assert(test_round_higher_precision_scales_up());
static_assert(test_round_to_zero_precision());

int main() {
    // ── Exception tests (runtime-only) ───────────────────────────────────────

    // negative precision
    CHECK_EXCEPTION(std::invalid_argument, Decimal(1, -1));
    CHECK_EXCEPTION(std::invalid_argument, Decimal(1.0, -1));

    // addition overflow
    CHECK_EXCEPTION(std::overflow_error,
        Decimal(INT64_MAX / 2 + 2, 0) + Decimal(INT64_MAX / 2 + 2, 0));

    // scale overflow during operand scaling
    CHECK_EXCEPTION(std::overflow_error,
        Decimal(INT64_MAX / 5, 0) + Decimal(1, 1));

    // multiplication overflow
    CHECK_EXCEPTION(std::overflow_error,
        Decimal(INT64_MAX / 2, 0) * Decimal(3, 0));

    // division by zero
    CHECK_EXCEPTION(std::domain_error,
        Decimal(100, 2) / Decimal(0, 2));

    // round_to negative precision
    CHECK_EXCEPTION(std::invalid_argument,
        Decimal(123, 2).round_to(-1));

    return 0;
}
