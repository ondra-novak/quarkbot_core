#pragma once

#include "../common/basic_context.h"
#include "../common/priority_queue.h"


#include <memory>
namespace quarkbot {

namespace simulator {

class Controller {
public:



protected:

    struct StrategyItem {
        std::unique_ptr<BasicContext> _ctx;
        Timestamp _tp;
    };

    struct StrategyItemCmp {
        bool operator()(const StrategyItem &a, const StrategyItem &b) const {
            return a._tp > b._tp;
        }
    };

    PriorityQueue<StrategyItem, StrategyItemCmp> _strategy_queue;

};

}


}
