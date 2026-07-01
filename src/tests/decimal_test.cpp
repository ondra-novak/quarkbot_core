#include <quarkbot/decimal.hpp>
#include <iostream>
#include <iterator>
#include <vector>

constexpr std::string_view to_string_number(Decimal m, std::vector<char> &buff, int decimals) {
    buff.clear();
    m.to_string_fixed(std::back_inserter(buff), decimals);
    return {buff.data(), buff.size()};
}

constexpr bool test_string(Decimal m, int decimals,  std::string_view text) {
    std::vector<char> buff;
    auto r = to_string_number(m,  buff, decimals);
    return r == text;
}


static_assert(Decimal::normalize(12345678, 0).exponent() == -8);
static_assert(Decimal::normalize(123456789, 0).exponent() == -7);
static_assert(Decimal::normalize(12345678912345678LL, 0).exponent() == 1);
static_assert(Decimal::normalize(12345678912345678LL, 0).mantissa() == 1234567891234567);
static_assert(Decimal::normalize(-12345678912345678LL, 0).mantissa() == -1234567891234567);
static_assert(Decimal::normalize(12345678955578LL, 10).exponent() == 8);

static_assert(Decimal(567).exponent() == 3 ); //0.567 * 10^3
static_assert(Decimal(567).mantissa() == 5670000000000000LL ); 
static_assert(Decimal(-567).exponent() == 3 ); //0.567 * 10^3
static_assert(Decimal(-567).mantissa() == -5670000000000000LL ); 

static_assert(Decimal::round_mantissa(155, 1) == 16);
static_assert(Decimal::round_mantissa(555555555, 9) == 1);
static_assert(Decimal::round_mantissa(444444444, 9) == 0);
static_assert(Decimal::round_mantissa(555555555, 10) == 0);
static_assert(Decimal::round_mantissa(1544, 2) == 15);
static_assert(Decimal::round_mantissa(-154, 1) == -15);
static_assert(Decimal::round_mantissa(-1566, 2) == -16);

static_assert((1.00_dec).mantissa() == 1000000000000000);
static_assert((Decimal(1)+Decimal(2)) == 3_dec);
static_assert((Decimal(1)+Decimal(2)).exponent() == 1);

static_assert((Decimal(1024)+Decimal(2)) == 1026_dec);
static_assert((Decimal(5)+Decimal(12878)) == 12883_dec);

static_assert((Decimal(3)-Decimal(2)) == 1_dec);
static_assert((Decimal(10)-Decimal(5)) == 5_dec);
static_assert((Decimal(10)-Decimal(5)).exponent() == 1);
static_assert((Decimal(10)-Decimal(20)).exponent() == 2);
static_assert((Decimal(10)-Decimal(20)) == -10_dec);
static_assert((Decimal(16)*Decimal(42)) == 672_dec);
static_assert((Decimal(16)*Decimal(42)).exponent() == 3);
static_assert((Decimal(123456789)*Decimal(246871231)).mantissa() == 3047792947573725);
static_assert((Decimal(123456789)*Decimal(246871231)).exponent() == 17);
static_assert((Decimal(-123456789)*Decimal(246871231)).mantissa() == -3047792947573725);


static_assert((Decimal(1024)/Decimal(16)).mantissa() == 6400000000000000);
static_assert((Decimal(1024)/Decimal(16)).exponent() == 2);
static_assert((Decimal(Decimal::mantissa_max,10)/Decimal(23)).mantissa() == 4347826086956520);
static_assert((Decimal(Decimal::mantissa_max,10)/Decimal(23)).exponent() == 9);

static_assert((-Decimal(11)/Decimal(2)).mantissa() == -5500000000000000);
static_assert((-Decimal(11)/Decimal(2)).exponent() == 1);
static_assert((Decimal(1)/Decimal(123)).mantissa() == 8130081300813009);
static_assert((Decimal(1)/Decimal(123)).exponent() == -2);
static_assert(Decimal(1) < Decimal(2));
static_assert(Decimal(1) < Decimal(200));
static_assert(Decimal(1000) > Decimal(200));
static_assert(Decimal(-1000) < Decimal(200));
static_assert(Decimal(-1000) < Decimal(200));
static_assert(round(3.14153265_dec) == 3);
static_assert(round(3.75812378_dec) == 4);
static_assert(round(0.375812378_dec) == 0);
static_assert(round(0.666666666_dec) == 1);
static_assert(round(125784.666_dec) == 125785);
static_assert(round(-125784.666_dec) == -125785);
static_assert(round(scaleb10(-125784125_dec,20)) == scaleb10(-125784125_dec,20));
static_assert(ceil(3.14153265_dec) == 4);
static_assert(floor(3.14153265_dec) == 3);
static_assert(ceil(-3.75812378_dec) == -3);
static_assert(floor(-3.75812378_dec) == -4);
static_assert(ceil(3_dec) == 3);
static_assert(floor(3_dec) == 3);
static_assert(ceil(0_dec) == 0);
static_assert(floor(0_dec) == 0);
static_assert(ceil(Decimal(2)) == 2);
static_assert(ceil(Decimal(-2)) == -2);
static_assert(floor(Decimal(2)) == 2);

static_assert(3.141592_dec == Decimal(3141592000000000LL,1));
static_assert(-3.141592_dec == Decimal(-3141592000000000LL,1));
static_assert((12e+12_dec) == Decimal(1200000000000000LL,14));
static_assert((12.476e+12_dec) == Decimal(1247600000000000LL,14));


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
static_assert(test_string(0_dec, 2, "0.00"));

static_assert(static_cast<int>(123e4_dec) == 1230000);
static_assert(static_cast<int>(123e-1_dec) == 12);
static_assert(static_cast<int>(-123e4_dec) == -1230000);
static_assert(static_cast<int>(-123e-1_dec) == -12);

static_assert(ceil(0.00000032_dec) == 1_dec);
static_assert(ceil(-0.00000032_dec) == 0_dec);
static_assert(floor(0.00000032_dec) == 0_dec);
static_assert(floor(-0.00000032_dec) == -1_dec);
static_assert(round(0.00000062_dec) == 0_dec);
static_assert(round(-0.00000082_dec) == 0_dec);


int main() {    
    std::vector<char> buff;
    std::cout << Decimal(-3.141592).to_string() << std::endl;
    std::cout << Decimal(12887841235.5787456315679).to_string() << std::endl;
    std::cout << Decimal(-1287e53_dec).to_string() << std::endl;
    std::cout << Decimal(-12414658e-5_dec).to_string() << std::endl;
    std::cout << Decimal(-11258e-25_dec).to_string() << std::endl;
    std::cout << 0_dec << std::endl;
    std::cout << 0.412_dec << std::endl;
}


