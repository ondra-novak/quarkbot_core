#pragma once

#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/abstract/ieventstream.hpp"
#include "quarkbot/timestamp.hpp"
namespace quarkbot {

///collect multiple history sources and choose one depend on query - assumes they are not overlapping
class HistorySourceCollector: public std::vector<BacktestHistorySource> {
public:

    std::shared_ptr<IEventStreamBase> operator()(const PMarketInstrument &instr, std::size_t class_hash, 
                                const HistoryDataRequest& query, const Timestamp &sim_time) const {
        for (const auto &x: *this) {
            auto ifc = x(instr, class_hash, query, sim_time);
            if (ifc) return ifc;
        }
        return {};
    }

};


}