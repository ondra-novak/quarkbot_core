#include "merged_data_source.hpp"
#include <algorithm>

namespace quarkbot {

MergedDataSource::MergedDataSource(std::vector<std::shared_ptr<IBacktestDataSource>> sources)
    : _sources(std::move(sources)) {
    for (std::size_t i = 0; i < _sources.size(); ++i) {
        auto ev = _sources[i]->next_event();
        if (ev) _heap.push(PeekedEvent{std::move(*ev), i});
    }
}

std::optional<IBacktestDataSource::Event> MergedDataSource::next_event() {
    if (_heap.empty()) return std::nullopt;
    auto top = _heap.top();
    _heap.pop();
    auto refill = _sources[top.source_idx]->next_event();
    if (refill) _heap.push(PeekedEvent{std::move(*refill), top.source_idx});
    return std::move(top.event);
}

std::vector<InstrumentSpec> MergedDataSource::get_instrument_infos() {
    std::vector<InstrumentSpec> result;
    for (auto &src : _sources) {
        for (auto &spec : src->get_instrument_infos()) {
            bool dup = std::any_of(result.begin(), result.end(),
                [&](const InstrumentSpec &s){ return s.name == spec.name; });
            if (!dup) result.push_back(std::move(spec));
        }
    }
    return result;
}

} // namespace quarkbot
