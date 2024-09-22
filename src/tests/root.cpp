#include <quarkbot/ta/root.h>
#include "check.h"

int main() {

    auto r = quarkbot::bisection_root(1.0, 2.0, [&](double x){return x*x*x -x -2;}, 30);
    CHECK(r.has_value());
    CHECK_BETWEEN(1.5213, *r, 1.5214);

    r = quarkbot::bisection_root(2.0, 3.0, [&](double x){return x*x*x -x -2;}, 30);
    CHECK(!r.has_value());

    r = quarkbot::bisection_root(-3.0, 3.0, [&](double x){return -(x*x*x)-3*x*x + 2;}, 30);
    CHECK(r.has_value());
    CHECK_BETWEEN(-1.0001, *r, -0.99999);


}
