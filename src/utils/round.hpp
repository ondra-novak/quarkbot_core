#pragma once

#include "utils/decimal.hpp"
#include <cmath>
namespace quarkbot {
    enum class RoundStrategy {
    ///round to lower value
    floor,
    ///round to upper value
    ceil,
    ///round to nearest
    nearest,
    ///rounding to a value that represents less risk
    defensive,
    ///rounding to a value that represents more risk
    aggresive
};

///Represents rounded number with specified round strategy
class Rounded {
public:
    Decimal value = 0;
    RoundStrategy strategy = RoundStrategy::nearest;
    
    constexpr Rounded() = default;

    constexpr Rounded(Decimal v, RoundStrategy s = RoundStrategy::nearest): value(v), strategy(s) {}

    ///get rounded number
    /**
    @param step round step 
    @param defensive_side specifies side which is defensive (other side is aggresive). Positive value
    means to use ceil for defensive side and floor for agresive side. If defensive side is zero,
    round is used in this case
     */
    Decimal get_rounded(Decimal step, int defesive_side) const {
        auto v = value * step;
        switch (strategy) {
            case RoundStrategy::floor:
                return floor(v)/step;
            case RoundStrategy::ceil:
                return ceil(v)/step;
            case RoundStrategy::nearest:
                return round(v)/step;
            case RoundStrategy::defensive:
                if (defesive_side < 0) {
                    return floor(v)/step;
                } else if (defesive_side > 0) { 
                    return ceil(v)/step;
                } else {
                    return round(v)/step;
                }
            case RoundStrategy::aggresive:
                if (defesive_side < 0) {
                    return ceil(v)/step;
                } else if (defesive_side > 0) { 
                    return floor(v)/step;
                } else {
                    return round(v)/step;
                }
        }
        return value;
    }
};

}