#pragma once
#include "quarkbot/abstract/backtest_data_source.hpp"
#include <queue>
#include <vector>

namespace quarkbot {

class MergedDataSource  {
public:
    explicit MergedDataSource(std::vector<BacktestDataSource> sources);
    bool operator()(BacktestEvent &event);

private:
    struct PeekedEvent {
        BacktestEvent event;
        std::size_t source_idx;
        bool operator>(const PeekedEvent &o) const { return event.time > o.event.time; }
    };

    std::vector<BacktestDataSource> _sources;
    std::priority_queue<PeekedEvent, std::vector<PeekedEvent>, std::greater<PeekedEvent>> _heap;
};

} // namespace quarkbot
