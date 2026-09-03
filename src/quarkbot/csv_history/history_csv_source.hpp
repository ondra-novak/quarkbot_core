#pragma once

#include "quarkbot/abstract/backtest_data_source.hpp"
#include "../backtest/symbology_mapping.hpp"
#include "quarkbot/abstract/ieventstream.hpp"
#include "quarkbot/stream/history.hpp"
#include "quarkbot/timestamp.hpp"
#include "quarkbot/utils/lookup.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>
namespace quarkbot {

///Historical data served from a set of CSV files, one file per symbol
/**
 * The files are located through an index file (`symbol,file`, paths relative to the
 * index file). Every file of one source holds the same type of data at the same
 * interval, both declared here; a backtest that needs more than one type declares
 * more than one source.
 *
 * See formats.md for the layout of the index and of the data files.
 */
struct HistoryCSVSourceConfig {

    ///Type of data in the files of this source, and thus the streams it can serve
    enum Type {
        ///`time,open,high,low,close,volume[,trades]` - serves ClosedBar
        ohlc,
        ///`time,close,volume` - serves Trade
        close,
        ///`time,open_price,open_volume,close_price,close_volume` - serves AuctionDailyHistory
        auction,
        ///`time,ask_price,ask_volume,bid_price,bid_volume` - serves Quote
        quote,
        ///quote columns plus `price,volume` - serves Quote and Trade (rows with price 0 carry no trade)
        l1
    };

    using Index = std::unordered_map<std::string, std::filesystem::path>;
    ///interval of one record, interval_undefined for tick data (quote, close, l1, auction)
    HistoryDataRequest::Interval interval = HistoryDataRequest::interval_undefined;
    ///type of the data in the files
    Type type = ohlc;
    ///path to the index file mapping symbols to data files
    std::filesystem::path index_file;
    ///the loaded index, filled by reload_index() on the first request
    std::optional<Index> symbol_index;
    ///renames a symbol of the index file to the symbol the strategy uses, false drops the entry
    std::function<bool(std::string &)> symbology_translate;

    template<SymbologyMap _Map>
    void set_symbology_map_ignore_missing(_Map map) {
        symbology_translate = [map = std::move(map)](std::string &n) {
            auto iter = map.find(n);
            if (iter != map.end()) n = iter->second;
            return true;
        };
    }

    template<SymbologyMap _Map>
    void set_symbology_map_remove_missing(_Map map) {
        symbology_translate = [map = std::move(map)](std::string &n) {
            auto iter = map.find(n);
            if (iter != map.end()) {n = iter->second;return true;}
            return false;
        };
    }


    ///Reads the index file into symbol_index
    /**
     * @exception std::runtime_error the index file cannot be read or has a wrong format
     */
    void reload_index();

    ///Opens the file of the symbol and creates a stream of its records
    /**
     * @param symbol symbol as the strategy knows it (after symbology translation)
     * @param type_hash class_hash of the requested event type
     * @param start_date first day to report (inclusive)
     * @param end_date last day to report (inclusive)
     * @param start_time offset from the midnight of start_date
     * @param backtest_cut_time the simulation time - a record which becomes known after it
     *        is not reported, so that a backtest never sees the future. A bar becomes known
     *        when it closes, any other record when it happens.
     * @return the stream, or nullptr when this source has no data of the requested
     *         type for the symbol
     *
     * @note backtest_cut_time must be adjusted to time zone of the instrument
     */
    std::shared_ptr<IEventStreamBase> get_stream(
            const std::string &symbol,
            std::size_t type_hash,
            std::chrono::year_month_day start_date,
            std::chrono::year_month_day end_date,
            std::chrono::hh_mm_ss<std::chrono::seconds> start_time,
            Timestamp backtest_cut_time);



};

template<>
inline constexpr auto string_lookup<HistoryCSVSourceConfig::Type> = make_string_lookup_table<HistoryCSVSourceConfig::Type>({
    {HistoryCSVSourceConfig::ohlc,"ohlc"},
    {HistoryCSVSourceConfig::close,"close"},
    {HistoryCSVSourceConfig::auction,"auction"},
    {HistoryCSVSourceConfig::quote,"quote"},
    {HistoryCSVSourceConfig::l1,"l1"},
});


///Wraps the configuration into a BacktestHistorySource usable by the simulated exchange
inline BacktestHistorySource create_csv_history_source(HistoryCSVSourceConfig config) {
        return [config = std::move(config)](const PMarketInstrument &instr, std::size_t class_hash,
                        const HistoryDataRequest& query, const Timestamp &sim_time) mutable -> std::shared_ptr<IEventStreamBase> {
            if (!instr) return {};
            if (query.interval != config.interval) return {};
            const auto &info = instr->get_info();
            return config.get_stream(info.name, class_hash, query.start_date, query.end_date, query.start_time , sim_time);
        };

    }





}
