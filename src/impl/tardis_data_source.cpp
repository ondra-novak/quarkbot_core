#include "tardis_data_source.hpp"

namespace quarkbot {

std::optional<IBacktestDataSource::Event> TardisTradesDataSource::next_event() {
    return std::nullopt;  // stub — implemented in Task 3
}

std::optional<IBacktestDataSource::Event> TardisQuotesDataSource::next_event() {
    return std::nullopt;  // stub — implemented in Task 4
}

} // namespace quarkbot
