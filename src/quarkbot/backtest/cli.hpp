#pragma once

#include "quarkbot/context.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/strategy_main.hpp"
namespace quarkbot {

class StrategyContext;

int start_indirect(int argc, char **argv, StrategyFragment (*startup_fn)(StrategyContext &&));

template<StrategyClass<StrategyContext> Strategy>
int start(int argc, char **argv) {
    return start_indirect(argc, argv, [](StrategyContext &&ctx){
        return create_and_start_strategy<Strategy, StrategyContext>(std::move(ctx));
    });
}






}