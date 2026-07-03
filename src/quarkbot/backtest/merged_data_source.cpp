#include "merged_data_source.hpp"

namespace quarkbot {

MergedDataSource::MergedDataSource(std::vector<BacktestDataSource> sources)
    : _sources(std::move(sources)) {
    BacktestEvent ev;
    for (std::size_t i = 0; i < _sources.size(); ++i) {
        bool ok = _sources[i](ev);
        if (ok) _heap.push(PeekedEvent{std::move(ev), i});
    }
}

// Note: events with equal timestamps are returned in unspecified order
// (std::priority_queue provides no stability guarantee).
bool MergedDataSource::operator()(BacktestEvent &event) {
    if (_heap.empty()) return false;
    auto top = std::move(const_cast<PeekedEvent&>(_heap.top())); // safe: popped immediately below
    _heap.pop();
    bool refill = _sources[top.source_idx](event);
    if (refill) _heap.push(PeekedEvent{std::move(event), top.source_idx});
    event = std::move(top.event);
    return true;
}


} // namespace quarkbot
