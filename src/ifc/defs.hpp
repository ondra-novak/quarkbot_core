#pragma  once

#include <cmath>
#include <memory>
#include "../coro/src/basic_coro/awaitable.hpp"


namespace quarkbot {

template<typename T> using awaitable = coro::awaitable<T>;

struct StreamTypeItem {};

template<typename T>
concept StreamType = std::is_base_of_v<StreamTypeItem, T>;


class IAccount;
class IExchange;
class IMarketInstrument;
class ITradableInstrument;
class IOrder;
class IUnderlyingCurrency;
class IStorage;
class IScheduler;
template<StreamType T>
class IMarketEventStream;


using PAccount = std::shared_ptr <IAccount>;
using PExchange = std::shared_ptr<IExchange>;
using PMarketInstrument = std::shared_ptr<IMarketInstrument>;
using PTradableInstrument = std::shared_ptr<ITradableInstrument>;
using POrder = std::shared_ptr<IOrder>;
using PUnderlyingCurrency = std::shared_ptr<IUnderlyingCurrency>;
using PStorage = std::shared_ptr<IStorage>;
template<StreamType T>
using PMarketEventStream = std::shared_ptr<IMarketEventStream<T> >;
using PScheduler = std::shared_ptr<IScheduler>;

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
class RoundedNumber {
public:
    double value = 0;
    RoundStrategy strategy = RoundStrategy::nearest;
    
    constexpr RoundedNumber() = default;
    ///construct number
    constexpr RoundedNumber(double v, RoundStrategy s = RoundStrategy::nearest): value(v), strategy(s) {}

    ///get rounded number
    /**
    @param step round step 
    @param defensive_side specifies side which is defensive (other side is aggresive). Positive value
    means to use ceil for defensive side and floor for agresive side. If defensive side is zero,
    round is used in this case
     */
    double get_rounded(double step, int defesive_side) const {
        double v = value * step;
        switch (strategy) {
            case RoundStrategy::floor:
                return std::floor(v)/step;
            case RoundStrategy::ceil:
                return std::ceil(v)/step;
            case RoundStrategy::nearest:
                return std::round(v)/step;
            case RoundStrategy::defensive:
                if (defesive_side < 0) {
                    return std::floor(v)/step;
                } else if (defesive_side > 0) { 
                    return std::ceil(v)/step;
                } else {
                    return std::round(v)/step;
                }
            case RoundStrategy::aggresive:
                if (defesive_side < 0) {
                    return std::ceil(v)/step;
                } else if (defesive_side > 0) { 
                    return std::floor(v)/step;
                } else {
                    return std::round(v)/step;
                }
        }
        return value;
    }
};


}