#pragma once

#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/abstract/ihistory.hpp"
#include "quarkbot/defs.hpp"
namespace quarkbot {

    class SimHistoryAdapter: public IHistoryAdapter {
    public:

        SimHistoryAdapter(BacktestHistorySource source,PMarketInstrument instrument)
            :_source(std::move(source)), _instrument(std::move(instrument)) {}

        std::shared_ptr<IEventStreamBase> subscribe_stream(std::size_t class_hash, const void *params) override;

    protected:
        BacktestHistorySource _source;
        PMarketInstrument _instrument;
    };

}