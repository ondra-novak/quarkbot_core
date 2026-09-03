#include "merged_data_source.hpp"
#include "quarkbot/algoseek/algoseek_spec.hpp"
#include "quarkbot/backtest/history_collector.hpp"
#include "quarkbot/csv_history/history_csv_source.hpp"
#include "quarkbot/log.hpp"
#include "quarkbot/stream/history.hpp"
#include "quarkbot/types.hpp"
#include "quarkbot_compile_config.h"
#include "config_datasource.hpp"
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include "quarkbot/utils/string_utils.hpp"
#include "replay_csv_file.hpp"
#include "symbology_mapping.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../algoseek/algoseek_data_source.hpp"
#include "../tardis/tardis_data_source.hpp"
#include "../trth/trth_event_source.hpp"

#include "minute_data_source.hpp"


namespace quarkbot {



class SourceCollector {
public:

    SourceCollector(std::string_view data_section_name, std::string_view history_section_name, std::string_view mapping_section_name)
        :data_section_name(data_section_name), history_section_name(history_section_name), mapping_section_name(mapping_section_name) {}
    

    std::unordered_set<std::filesystem::path> processed;
    std::vector<BacktestDataSource> sources;
    std::unordered_map<std::string, std::string> mapping;
    std::vector<HistoryCSVSourceConfig> history_sources;
    std::string_view data_section_name;
    std::string_view history_section_name;
    std::string_view mapping_section_name;

    struct Reader {
        std::string_view data;
        std::string_view operator()() {return std::exchange(data,"");}
    };
    using IniCfg = IniReader<Reader>;
    using Row = IniCfg::Row;


    void walk(std::filesystem::path file) {
        std::vector<std::filesystem::path> algoseek_sources;
        AlgoseekSpec algoseek_spec;
        std::optional<HistoryCSVSourceConfig> history_cfg;


        auto fcan = std::filesystem::canonical(file);
        auto root = fcan.parent_path();
        if (!processed.insert(fcan).second) return; //break cycle if already processed
        std::ifstream f(fcan);
        std::string content;
        std::copy(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>(), std::back_inserter(content));

        IniCfg ini({content});
        Row row;
        while (ini.next(row)) {
            if (row.section == data_section_name) {
                if (row.key == "quarkbot") add_quarkbot(root/row.value);
                else if (row.key.starts_with("tardis.")) {
                    auto t = row.key.substr(7);
                    if (t == "trades") sources.push_back(TardisTradesDataSource(root/row.value));
                    else if (t == "quotes") sources.push_back(TardisQuotesDataSource(root/row.value));
                    else throw std::runtime_error(std::format(
                        "Unknown tardis data type: `{}`, Expected: trades, quotes in config `{}`",
                        row.key, fcan.string()));
                }
                else if (row.key == "tardis") throw std::runtime_error(std::format(
                    "Key `tardis` is no longer supported, use `tardis.trades` or "
                    "`tardis.quotes` in config `{}`", fcan.string()));
                else if (row.key == "lseg" || row.key == "trth") add_trth(root/row.value);
                else if (row.key.starts_with("algoseek.")) {
                    auto  t= row.key.substr(9);
                    if (t == "time_zone") {
                        try {
                            algoseek_spec.tz = std::chrono::locate_zone(row.value);
                        } catch (const std::exception &e) {
                            throw std::runtime_error(
                                    std::format("Unknown time zone `{}` in key `{}` in config `{}`: {}",
                                            row.value, row.key, fcan.string(), e.what()));
                        }
                    } else if (t == "exchange") {
                        algoseek_spec.exchange = row.value;
                    } else if (t == "symbol") {
                        algoseek_spec.symbol = row.value;
                    } else if (t == "fake_quotes") {
                        try {
                            algoseek_spec.fake_quotes_distance = Decimal::from_string(row.value);
                        } catch (const std::exception &e) {
                            throw std::runtime_error(
                                    std::format("Invalid number format `{}` in key `{}` in config `{}`: {}",
                                            row.value, row.key, fcan.string(), e.what()));
                        }
                    } else {
                        throw std::runtime_error(
                                std::format("Unknown algoseek option: `{}`, Expected: time_zone, exchange, symbol, fake_quotes in config `{}`", row.key, fcan.string()));
                    }
                }

                else if (row.key == "algoseek") algoseek_sources.push_back(root/row.value);
                else throw std::runtime_error(std::format("Unknown key {} in config {}", row.key, fcan.string()));
            } else if (row.section == mapping_section_name) {
                std::string_view from_symbol;
                std::string_view to_symbol;
                if (row.key.ends_with('<')) {
                    to_symbol = trim(row.key.substr(0, row.key.length()-1));
                    from_symbol = row.value;
                } else if (row.value.starts_with('>')) {
                    to_symbol = trim(row.value.substr(1));
                    from_symbol = row.key;
                } else {
                    throw std::runtime_error(
                    std::format("Mapping direction is not specified: {} => {} in config file: {}", row.key, row.value, fcan.string()));
                }
                if (!mapping.emplace(from_symbol, to_symbol).second) {
                    throw std::runtime_error(
                    std::format("Duplicate mapping for: {} in config file: {}", from_symbol, fcan.string()));
                }
            } else if (row.section == history_section_name) {
                if (!history_cfg) history_cfg.emplace();
                if (row.key == "type") {
                    auto t = string_lookup<HistoryCSVSourceConfig::Type>(row.value);
                    if (!t) throw std::runtime_error(std::format("Enum {} is not in list [{}]", row.value, lookup_available_options(string_lookup<HistoryCSVSourceConfig::Type>)));
                    history_cfg->type = t.value();
                } else if (row.key == "index") {
                    history_cfg->index_file = root/row.value;
                } else if (row.key == "interval") {
                    history_cfg->interval = static_cast<HistoryDataRequest::Interval>(std::stoll(std::string(row.value)));                    
                }
            } else if (row.section.empty() && row.key == "include") {
                walk(root/row.value);
            }
        }
        if (!algoseek_sources.empty()) {
            if (algoseek_spec.exchange.empty() && !algoseek_spec.tz && algoseek_spec.symbol.empty()) {
                logWarning("Algoseek data source(s) specified in config {} without any options; this is probably a mistake. Following keys should be specified at least: algoseek.time_zone, algoseek.exchange", fcan.string());
            }
            for (auto &s: algoseek_sources) {
                auto spec = algoseek_spec;
                spec.file = s;
                sources.push_back(AlgoseekDataSource(std::move(spec)));
            }
        }
        if (history_cfg.has_value()) {
            if (!history_cfg->index_file.empty() && std::filesystem::exists(history_cfg->index_file)) {
                history_sources.push_back(std::move(history_cfg).value());
            } else {
                throw std::runtime_error("Bad history source definition");
            }
        }
    }

    void add_trth(std::filesystem::path file) {
        sources.push_back(TRTHEventSource(file));
        
    }
    void add_quarkbot(std::filesystem::path file) {
        sources.push_back(ReplayCSVDataSource(file));
    }


    BacktestDataSource build_raw() {
        if (sources.empty()) throw std::runtime_error("No data sources defined");
        if (sources.size() == 1) return std::move(sources[0]);
        else return MergedDataSource(std::move(sources));
    }

    BacktestDataSource build(SymbologyMapMode smm) {
        if (mapping.empty() || smm == SymbologyMapMode::no_mapping) return build_raw();
        else if (smm == SymbologyMapMode::skip_missing) {
            return SymbologyMapping_SkipMissing<decltype(mapping), BacktestDataSource>(mapping, build_raw());
        } else {
            return SymbologyMapping_IgnoreMissing<decltype(mapping), BacktestDataSource>(mapping, build_raw());
        } 
    }
    BacktestHistorySource history_build(SymbologyMapMode smm) {
        HistorySourceCollector sources;
        for (auto &s: history_sources) {
            if (!mapping.empty()) {
                if (smm == SymbologyMapMode::skip_missing) {
                    s.set_symbology_map_remove_missing(mapping);    
                } else {
                    s.set_symbology_map_ignore_missing(mapping);    
                }
            }
            sources.push_back(create_csv_history_source(s));
        }
        return sources;
    }

};




std::pair<BacktestDataSource, BacktestHistorySource> configure_datasources(std::filesystem::path ini_config,
        SymbologyMapMode smm,
        std::string_view data_section ,
        std::string_view history_section,
        std::string_view symbology_mapping_section 
) {

    SourceCollector coll(data_section, history_section, symbology_mapping_section);
    coll.walk(ini_config);
    return {coll.build(smm), coll.history_build(smm)};

}

}