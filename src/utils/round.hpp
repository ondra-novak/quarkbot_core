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
class TargetValue {
public:
    Decimal value = 0;
    RoundStrategy strategy = RoundStrategy::nearest;
    
    constexpr TargetValue() = default;

    constexpr TargetValue(Decimal v, RoundStrategy s = RoundStrategy::nearest): value(v), strategy(s) {}

    ///get rounded number
    /**
    @param step round step 
    @param aggresive_side specifies side which is aggresive (other side is defensive). Positive value
    means to use ceil for defensive side and floor for agresive side. If aggresive_side is zero,
    round is used in this case
     */
    Decimal get_rounded(Decimal step, int aggresive_side) const {
        auto v = value * step;
        switch (strategy) {
            case RoundStrategy::floor:
                return floor(v)/step;
            case RoundStrategy::ceil:
                return ceil(v)/step;
            case RoundStrategy::nearest:
                return round(v)/step;
            case RoundStrategy::defensive:
                if (aggresive_side > 0) {
                    return floor(v)/step;
                } else if (aggresive_side < 0) { 
                    return ceil(v)/step;
                } else {
                    return round(v)/step;
                }
            case RoundStrategy::aggresive:
                if (aggresive_side > 0) {
                    return ceil(v)/step;
                } else if (aggresive_side < 0) { 
                    return floor(v)/step;
                } else {
                    return round(v)/step;
                }
        }
        return value;
    }
};

}