#pragma once
#include "ifc/backtest_data_source.hpp"
#include <memory>
#include <queue>
#include <vector>

namespace quarkbot {

class MergedDataSource : public IBacktestDataSource {
public:
    explicit MergedDataSource(std::vector<std::shared_ptr<IBacktestDataSource>> sources);
    std::optional<Event> next_event() override;
    std::vector<InstrumentSpec> get_instrument_infos() override;

private:
    struct PeekedEvent {
        Event event;
        std::size_t source_idx;
        bool operator>(const PeekedEvent &o) const { return event.time > o.event.time; }
    };

    std::vector<std::shared_ptr<IBacktestDataSource>> _sources;
    std::priority_queue<PeekedEvent, std::vector<PeekedEvent>, std::greater<PeekedEvent>> _heap;
};

} // namespace quarkbot
