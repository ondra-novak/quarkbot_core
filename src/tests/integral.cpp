#include <quarkbot/ta/integral.h>
#include "check.h"

int main() {



    auto pn = quarkbot::make_primitive_fn<10>(-10, 10, [](double x){return x;});
    CHECK_BETWEEN(7.499, (pn(4)-pn(-1)),7501);
    CHECK_BETWEEN(-20.001, (pn(-9)-pn(-11)),-19.999);
    CHECK_BETWEEN(19.999, (pn(11)-pn(9)),20.001);
    auto p2 = quarkbot::make_primitive_fn<1000>(-10, 10, [](double x){return x*x;});
    CHECK_BETWEEN(21.6666,p2(4)-p2(-1),21.6668);
    auto pd = quarkbot::make_primitive_fn<1000>(-10, 10, [](double x){return 1/x;});
    CHECK_BETWEEN(1.386,pd(4)-pd(1),1.387);

    return 0;
}
