#include "merged_data_source.hpp"
#include "quarkbot_compile_config.h"
#include "config_datasource.hpp"
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include "quarkbot/utils/string_utils.hpp"
#include "replay_csv_file.hpp"
#include "symbology_mapping.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
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

    SourceCollector(std::string_view data_section_name, std::string_view mapping_section_name)
        :data_section_name(data_section_name), mapping_section_name(mapping_section_name) {}
    

    std::unordered_set<std::filesystem::path> processed;
    std::vector<BacktestDataSource> sources;
    std::unordered_map<std::string, std::string> mapping;
    std::string_view data_section_name;
    std::string_view mapping_section_name;

    struct Reader {
        std::string_view data;
        std::string_view operator()() {return std::exchange(data,"");}
    };
    using IniCfg = IniReader<Reader>;
    using Row = IniCfg::Row;


    void walk(std::filesystem::path file) {
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
                else if (row.key == "tardis") add_tardis(root/row.value);
                else if (row.key == "lseg" || row.key == "trth") add_trth(root/row.value);
                else if (row.key == "algoseek") add_algoseek(root, row.value);
                else throw std::runtime_error(std::format("Unknown key {} in config {}", row.key, fcan.string()));
            } else if (row.section == mapping_section_name) {
                std::string_view from_symbol;
                std::string_view to_symbol;
                if (row.key.ends_with('<')) {
                    to_symbol = trim(row.key.substr(row.key.length()-1));
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

            } else if (row.section.empty() && row.key == "include") {
                walk(root/row.value);
            }
        }
    }

    void add_tardis(std::filesystem::path file) {
        throw std::runtime_error(std::format("Can't open {}: Tardis support is not yet implemented", file.string()));
    }
    void add_trth(std::filesystem::path file) {
        sources.push_back(TRTHEventSource(file));
        
    }
    void add_quarkbot(std::filesystem::path file) {
        sources.push_back(ReplayCSVDataSource(file));
    }
    ///the value carries a query string, so the path can only be joined after parsing
    void add_algoseek(const std::filesystem::path &root, std::string_view value) {
        auto spec = parse_algoseek_spec(value);
        spec.file = root / spec.file;
        sources.push_back(AlgoseekDataSource(std::move(spec)));
    }


    BacktestDataSource build_raw() {
        if (sources.empty()) throw std::runtime_error("No data sources defined");
        if (sources.size() == 1) return std::move(sources[0]);
        else return MergedDataSource(std::move(sources));
    }

    BacktestDataSource build(SymbologyMapMode smm) {
        if (mapping.empty() || smm == SymbologyMapMode::no_mapping) return build_raw();
        else if (smm == SymbologyMapMode::skip_missing) {
            return SymbologyMapping_SkipMissing<decltype(mapping), BacktestDataSource>(std::move(mapping), build_raw());
        } else {
            return SymbologyMapping_IgnoreMissing<decltype(mapping), BacktestDataSource>(std::move(mapping), build_raw());
        } 
    }

};




BacktestDataSource configure_datasources(std::filesystem::path ini_config,
        SymbologyMapMode smm,
        std::string_view data_section ,
        std::string_view symbology_mapping_section 
) {

    SourceCollector coll(data_section, symbology_mapping_section);
    coll.walk(ini_config);
    return coll.build(smm);

}

}