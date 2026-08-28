#pragma once

#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/exchange.hpp"
#include "quarkbot/json/json.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/stream/closedbar.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
namespace quarkbot {

class JsonReport {
public:

    JsonReport(std::ostream &outp);

    void attach_exchange(Exchange exchange, std::stop_token token, std::size_t interval);
    void attach_storage(Storage storage);

    enum class Event : char{
        //instrument info (at the beginning)
        instrument_info = 'I',
        //chart setup (interval)
        chart_setup='C',
        //chart (ohlc)
        chart='c',
        //fill info
        fill='f',
        //order status update
        order_status='o',
        //variable update
        var_update='v',
        //fill stats
        fill_stats='s',
    };

protected:
    std::ostream &_outp;

    static std::shared_ptr<std::ostream> open_file(std::filesystem::path filename);

    void out(Event event, const Json &json);

    StrategyFragment run_stream(EventStream<ClosedBar> stream, std::string name);

    ReportSink create_report_sink();

    Storage _storage;
    Storage::Replicator::Connection _storage_report;
};

}