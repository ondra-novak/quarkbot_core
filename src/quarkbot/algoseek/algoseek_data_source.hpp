#pragma once

#include "algoseek_spec.hpp"
#include "local_time_converter.hpp"

#include "quarkbot/abstract/backtest_data_source.hpp"
#include <quarkbot/utils/csv_reader.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace quarkbot {

///Bit positions in the Algoseek Conditions field used by this source
/**
    The field is a 32 bit mask written as 8 hexadecimal digits. Only the bits
    this source acts upon are named here; the full list is documented in
    algoseek.US.Equity.Trades.Only.pdf
*/
enum class AlgoseekTradeFlag: unsigned {
    ///opening auction print
    opening_prints = 6,
    ///closing auction print
    closing_prints = 7,
    ///reopening auction print, follows a trading halt
    reopening_prints = 8,
    ///official closing price re-broadcast, not a trade
    official_close = 24,
    ///official opening price re-broadcast, not a trade
    official_open = 26,
};

///test a single flag in a Conditions bitmask
constexpr bool has_flag(std::uint32_t flags, AlgoseekTradeFlag f) {
    return ((flags >> static_cast<unsigned>(f)) & 1U) != 0;
}

///Backtest data source generating Trade and Auction events from an Algoseek trades export
/**
    Reads one gzipped CSV file with the eight column layout
    Date,Timestamp,EventType,Ticker,Price,Quantity,Exchange,Conditions

    Only Trade and final Auction events are produced; the format carries no
    quotes and no indicative auction data. Rows that do not represent a
    tradable execution are counted and skipped, and the counters are logged
    when the file is exhausted.
*/
class AlgoseekDataSource {
public:

    ///open the source described by the specification
    /**
        @param spec parsed specification with a resolved file path
        @exception std::runtime_error file cannot be opened, or a required
            column is missing from the header
    */
    explicit AlgoseekDataSource(AlgoseekSpec spec);

    ///Retrieve next event
    /**
        @param ev reference to variable filled with event
        @retval true success
        @retval false eof reached
        @exception std::runtime_error malformed row
    */
    bool operator()(BacktestEvent &ev);

protected:

    struct CSVSource {
        std::move_only_function<std::string_view()> block_reader;
        std::string_view cur_line = {};
        int operator()();
    };

    struct Data {
        std::string date;
        std::string timestamp;
        std::string event_type;
        std::string ticker;
        std::string price;
        std::string quantity;
        std::string exchange;
        std::string conditions;
    };

    struct Counters {
        std::uint64_t trades = 0;
        std::uint64_t auctions = 0;
        std::uint64_t cancelled = 0;
        std::uint64_t unknown_event = 0;
        std::uint64_t filtered_exchange = 0;
        std::uint64_t zero_qty = 0;
        std::uint64_t zero_price = 0;
        std::uint64_t official_print = 0;
    };

    AlgoseekSpec _spec;
    LocalTimeConverter _tz;
    CSVReader<CSVSource> _csv;
    CSVFieldIndexMapping<Data> _colmap;
    Data _row = {};
    Counters _counters = {};
    ///number of the row last read; the header is row 1
    std::uint64_t _line = 1;
    bool _eof = false;

    static CSVSource init_source(const std::filesystem::path &file);
    static CSVFieldIndexMapping<Data> map_columns(CSVReader<CSVSource> &csv,
            const std::filesystem::path &file);

    ///throw a runtime_error naming the file and the current row
    [[noreturn]] void row_error(std::string_view message) const;
    ///parse the Conditions column, throws on anything but 8 hex digits
    std::uint32_t parse_conditions() const;
    ///combine the Date and Timestamp columns into a local timestamp
    std::chrono::local_time<std::chrono::nanoseconds> parse_local_time() const;
    ///log the skip counters
    void log_summary() const;
};

}
