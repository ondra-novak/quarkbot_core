#include "mydecimal.hpp"
#include <iostream>
#include <iterator>
#include <vector>

static_assert(MyDecimal::pow10c(3) == 1000);
static_assert(MyDecimal::pow10c(5) == 100000);
static_assert(MyDecimal::pow10c(-5) == 0.00001);

static_assert(MyDecimal::calc_normalize(123456789) == 0);
static_assert(MyDecimal::calc_normalize(123456789152LL) == -3);
static_assert(MyDecimal::calc_normalize(123456789123456789LL) == -9);
static_assert(MyDecimal::normalize(12345678, 0).exponent() == -1);
static_assert(MyDecimal::normalize(123456789, 0).exponent() == 0);
static_assert(MyDecimal::normalize(1234567891LL, 0).exponent() == 1);
static_assert(MyDecimal::normalize(1234567891LL, 0).manttisa() == 123456789);
static_assert(MyDecimal::normalize(-1234567891LL, 0).manttisa() == -123456789);
static_assert(MyDecimal::normalize(-1234567895LL, 0).manttisa() == -123456790);
static_assert(MyDecimal::normalize(1234567895LL, 0).manttisa() == 123456790);
static_assert(MyDecimal::normalize(12345678955578LL, 0).manttisa() == 123456790);
static_assert(MyDecimal::normalize(12345678955578LL, 10).exponent() == 15);

static_assert(MyDecimal(567).exponent() == 3 ); //0.567 * 10^3
static_assert(MyDecimal(567).manttisa() == 567000000 ); 
static_assert(MyDecimal(-567).exponent() == 3 ); //0.567 * 10^3
static_assert(MyDecimal(-567).manttisa() == -567000000 ); 
static_assert(MyDecimal::normalize(-1234567895LL, 0).manttisa() == -123456790);
static_assert(MyDecimal::normalize(-1234567894LL, 0).manttisa() == -123456789);

static_assert(MyDecimal::round_manttisa(155, 1) == 16);
static_assert(MyDecimal::round_manttisa(555555555, 9) == 1);
static_assert(MyDecimal::round_manttisa(444444444, 9) == 0);
static_assert(MyDecimal::round_manttisa(555555555, 10) == 0);
static_assert(MyDecimal::round_manttisa(1544, 2) == 15);
static_assert(MyDecimal::round_manttisa(-154, 1) == -15);
static_assert(MyDecimal::round_manttisa(-1566, 2) == -16);

static_assert((MyDecimal(1)+MyDecimal(2)).manttisa() == 300000000);
static_assert((MyDecimal(1)+MyDecimal(2)).exponent() == 1);

static_assert((MyDecimal(1024)+MyDecimal(2)).manttisa() == 102600000);
static_assert((MyDecimal(5)+MyDecimal(12878)).manttisa() == 128830000);

static_assert((MyDecimal(3)-MyDecimal(2)).manttisa() == 100000000);
static_assert((MyDecimal(10)-MyDecimal(5)).manttisa() == 500000000);
static_assert((MyDecimal(10)-MyDecimal(5)).exponent() == 1);
static_assert((MyDecimal(10)-MyDecimal(20)).exponent() == 2);
static_assert((MyDecimal(10)-MyDecimal(20)).manttisa() == -100000000);
static_assert((MyDecimal(16)*MyDecimal(42)).manttisa() == 672000000);
static_assert((MyDecimal(16)*MyDecimal(42)).exponent() == 3);
static_assert((MyDecimal(123456789)*MyDecimal(246871231)).manttisa() == 304779295);
static_assert((MyDecimal(123456789)*MyDecimal(246871231)).exponent() == 17);
static_assert((MyDecimal(-123456789)*MyDecimal(246871231)).manttisa() == -304779295);


static_assert((MyDecimal(1024)/MyDecimal(16)).manttisa() == 640000000);
static_assert((MyDecimal(1024)/MyDecimal(16)).exponent() == 2);
static_assert((MyDecimal(999999999,10)/MyDecimal(23)).manttisa() == 434782608);
static_assert((MyDecimal(999999999,10)/MyDecimal(23)).exponent() == 9);

static_assert((-MyDecimal(11)/MyDecimal(2)).manttisa() == -550000000);
static_assert((-MyDecimal(11)/MyDecimal(2)).exponent() == 1);
static_assert((MyDecimal(1)/MyDecimal(123)).manttisa() == 813008130);
static_assert((MyDecimal(1)/MyDecimal(123)).exponent() == -2);
static_assert(MyDecimal(1) < MyDecimal(2));
static_assert(MyDecimal(1) < MyDecimal(200));
static_assert(MyDecimal(1000) > MyDecimal(200));
static_assert(MyDecimal(-1000) < MyDecimal(200));
static_assert(MyDecimal(-1000) < MyDecimal(200));
static_assert(round(MyDecimal(314153265,1)) == 3);
static_assert(round(MyDecimal(375812378,1)) == 4);
static_assert(round(MyDecimal(375812378,0)) == 0);
static_assert(round(MyDecimal(666666666,0)) == 1);
static_assert(round(MyDecimal(125784666,6)) == 125785);
static_assert(round(MyDecimal(-125784666,6)) == -125785);
static_assert(round(MyDecimal(-125784125,15)) == MyDecimal(-125784125,15));
static_assert(ceil(MyDecimal(314153265,1)) == 4);
static_assert(floor(MyDecimal(314153265,1)) == 3);
static_assert(ceil(MyDecimal(-375812378,1)) == -3);
static_assert(floor(MyDecimal(-375812378,1)) == -4);
static_assert(ceil(MyDecimal(2)) == 3);
static_assert(ceil(MyDecimal(-2)) == -2);
static_assert(floor(MyDecimal(2)) == 2);
static_assert(floor(MyDecimal(299999999,1)) == 2);
static_assert(ceil(MyDecimal(299999999,1)) == 3);
static_assert(floor(MyDecimal(-299999999,1)) == -3);
static_assert(ceil(MyDecimal(-299999999,1)) == -2);

static_assert(3.141592_dec == MyDecimal(314159200,1));
static_assert(-3.141592_dec == MyDecimal(-314159200,1));
static_assert((12e+12_dec) == MyDecimal(120000000,13));
static_assert((12.476e+12_dec) == MyDecimal(124760000,13));

constexpr std::string_view to_string_number(MyDecimal m, std::vector<char> &buff, int decimals) {
    buff.clear();
    m.to_string_fixed(std::back_inserter(buff), decimals);
    return {buff.data(), buff.size()};
}

constexpr bool test_string(MyDecimal m, int decimals,  std::string_view text) {
    std::vector<char> buff;
    auto r = to_string_number(m,  buff, decimals);
    return r == text;
}

static_assert(test_string(1, 0, "1"));
static_assert(test_string(123, 0, "123"));
static_assert(test_string(0.1_dec, 1, "0.1"));
static_assert(test_string(0.124_dec, 3, "0.124"));
static_assert(test_string(0.5_dec, 3, "0.500"));
static_assert(test_string(123456789_dec, 3, "123456789.000"));
static_assert(test_string(12345678.9_dec, 3, "12345678.900"));
static_assert(test_string(12345678.9_dec, 0, "12345679"));
static_assert(test_string(1234567.89_dec, 1, "1234567.9"));
static_assert(test_string(123456.789_dec, 2, "123456.79"));


int main() {
    std::vector<char> buff;
    std::cout << to_string_number(12345678.9_dec, buff, 3) <<std::endl;
}


