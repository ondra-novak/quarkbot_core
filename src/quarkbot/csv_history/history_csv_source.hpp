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

struct HistoryCSVSourceConfig {
    
    enum Type {
        ohlc,
        close,
        auction,
        quote,
        l1
    };
    
    using Index = std::unordered_map<std::string, std::filesystem::path>;
    HistoryDataRequest::Interval interval;
    Type type;
    std::filesystem::path index_file;
    std::optional<Index> symbol_index;
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


    void reload_index();

    std::shared_ptr<IEventStreamBase> get_stream(
            const std::string &symbol,
            std::size_t type_hash,
            std::chrono::year_month_day start_date,
            std::chrono::year_month_day end_date,
            std::chrono::hh_mm_ss<std::chrono::seconds> start_time,
            Timestamp backtest_cut_time);   //note - backtest_cut_time must be adjusted to time zone of the instrument



};

template<>
inline constexpr auto string_lookup<HistoryCSVSourceConfig::Type> = make_string_lookup_table<HistoryCSVSourceConfig::Type>({
    {HistoryCSVSourceConfig::ohlc,"ohlc"},
    {HistoryCSVSourceConfig::close,"close"},
    {HistoryCSVSourceConfig::auction,"auction"},
    {HistoryCSVSourceConfig::quote,"quote"},
    {HistoryCSVSourceConfig::l1,"l1"},
});


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