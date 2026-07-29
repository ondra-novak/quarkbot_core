
#include "quarkbot/strategy_main.hpp"

namespace quarkbot {


    int entry_point(int argc, char **argv, std::function<StrategyFragment(StrategyContext &&, StrategyContext::Config &&)> start_fn){
        //TODO - argv[1] = strategy config, argv[2] = backtest config + python config
    }

}