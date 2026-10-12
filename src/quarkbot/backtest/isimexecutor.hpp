#pragma once

#include "quarkbot/strategy_fragment.hpp"
namespace quarkbot {

    class ISimExecutor {
    public:
        virtual ~ISimExecutor() = default;
        virtual bool cancel_all( PTradableInstrument instrument) = 0;
        virtual StrategyFragment place_order(POrder order) = 0;
        virtual StrategyFragment cancel_order(IOrder *ord) = 0;
    };


}