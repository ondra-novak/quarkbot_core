#include "config_instrument.hpp"
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "quarkbot/instrument_description.hpp"
#include "quarkbot/selector.hpp"
#include "quarkbot/types.hpp"
#include "quarkbot/underlying.hpp"
#include "quarkbot/utils/lookup.hpp"
#include "quarkbot/utils/simple_ini.hpp"
#include "quarkbot/utils/tagset.hpp"
namespace quarkbot {

class InstrumentCollector {
public:

    InstrumentCollector(std::string_view prefix)
        :prefix(prefix)
    {}


    std::string_view prefix;
    std::unordered_set<std::filesystem::path> processed;
    std::unordered_map<std::string, InstrumentDescription> instruments;

    using Field = std::variant<std::string InstrumentDescription::*,
                               UnderlyingCurrency InstrumentDescription::*,
                               std::optional<UnderlyingCurrency> InstrumentDescription::*,
                               InstrumentCategory InstrumentDescription::*,
                               InstrumentType InstrumentDescription::*,
                               std::size_t InstrumentDescription::*,
                               const std::chrono::time_zone *InstrumentDescription::*,
                               Decimal InstrumentDescription::*,
                               Decimal InstrumentGeometry::*>;

    static constexpr auto field_lookup = make_lookup_table<std::string_view, Field>({
        {"quote_currency", &InstrumentDescription::quote_currency},
        {"pnl_currency", &InstrumentDescription::pnl_currency},
        {"asset_wallet", &InstrumentDescription::asset_wallet},
        {"name", &InstrumentDescription::name},
        {"category", &InstrumentDescription::category},
        {"uid", &InstrumentDescription::uid},
        {"type", &InstrumentDescription::type},
        {"time_zone", &InstrumentDescription::time_zone},
        {"multiplier", &InstrumentDescription::multiplier},
        {"tick_scale", &InstrumentDescription::tick_scale},
        {"min_quantity",&InstrumentGeometry::min_quantity},
        {"max_quantity",&InstrumentGeometry::max_quantity},
        {"quantity_increment",&InstrumentGeometry::quantity_increment},
        {"price_increment",&InstrumentGeometry::price_increment},
        {"min_turnover",&InstrumentGeometry::min_turnover},
        {"leverage",&InstrumentGeometry::leverage},
        {"fee_rate_maker",&InstrumentGeometry::fee_rate_maker},
        {"fee_rate_taker",&InstrumentGeometry::fee_rate_taker},
    
    });


    struct Reader {
        std::string_view data;
        std::string_view operator()() {return std::exchange(data,"");}
    };
    using IniCfg = IniReader<Reader>;
    using Row = IniCfg::Row;

    static InstrumentType parse_type(std::string_view type)  {
        auto r = string_lookup<InstrumentType>(type);
        if (!r) throw std::runtime_error(
            std::format("Instrument type {} is not in allowed set: {}", 
                type, lookup_available_options(string_lookup<InstrumentType>)));
        return *r;
    }

    static InstrumentCategory parse_category(std::string_view cat) {
        auto r = string_lookup<InstrumentCategory>(cat);
        if (!r) throw std::runtime_error(
            std::format("Instrument category {} is not in allowed set: {}", 
                cat, lookup_available_options(string_lookup<InstrumentCategory>)));
        return *r;
    }

    void walk(std::filesystem::path file, const InstrumentDescription &default_values) {
        auto fcan = std::filesystem::canonical(file);
        auto root = fcan.parent_path();
        if (!processed.insert(fcan).second) return; //break cycle if already processed
        std::ifstream f(fcan);
        std::string content;
        std::copy(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>(), std::back_inserter(content));

        IniCfg ini({content});
        Row row;
        while (ini.next(row)) {
            if (row.section.starts_with(prefix)) {
                std::string name( row.section.substr(prefix.size()));
                auto ins = instruments.try_emplace(name);
                auto &desc = ins.first->second;
                if (ins.second) {
                    desc = default_values;
                    desc.name = name;
                }
                
                try {

                    auto fld = field_lookup(row.key);
                    if (!fld) {
                        
                        throw std::runtime_error(std::format("Unknown property: {} - expected {}", row.key, 
                            lookup_available_options(field_lookup)));
                    }
                    selector(*fld,
                        [&](Decimal InstrumentDescription::*ptr) {
                            desc.*ptr = Decimal::from_string(row.value);
                        },
                        [&](Decimal InstrumentGeometry::*ptr) {
                            desc.*ptr = Decimal::from_string(row.value);
                        },
                        [&](std::string InstrumentDescription::*ptr) {
                            desc.*ptr = row.value;
                        },
                        [&](UnderlyingCurrency InstrumentDescription::*ptr) {
                            (desc.*ptr).id = row.value;
                        },
                        [&](std::optional<UnderlyingCurrency> InstrumentDescription::*ptr) {
                            (desc.*ptr).emplace(UnderlyingCurrency{std::string(row.value)});
                        },
                        [&](InstrumentCategory InstrumentDescription::*ptr) {
                            auto s = string_lookup<InstrumentCategory>(row.value);
                            if (!s) throw std::runtime_error(std::format("Unknown instrument category: {} - expected: {}",
                                row.value, lookup_available_options(string_lookup<InstrumentCategory>)));                            
                            desc.*ptr = *s;
                        },
                        [&](InstrumentType InstrumentDescription::*ptr) {
                            auto s = string_lookup<InstrumentType>(row.value);
                            if (!s) throw std::runtime_error(std::format("Unknown instrument type: {} - expected: {}",
                                row.value, lookup_available_options(string_lookup<InstrumentType>)));                            
                            desc.*ptr = *s;
                            
                        },
                        [&](std::size_t InstrumentDescription::*ptr) {
                            std::size_t id;
                            auto res = std::from_chars(row.value.data(), row.value.data()+row.value.size(), id);
                            if (res.ec != std::errc{}) throw std::runtime_error(std::format("Invalid number: {}", row.value));
                            desc.*ptr = id;
                        },
                        [&](const std::chrono::time_zone *InstrumentDescription::*ptr) {
                            auto &tzdb = std::chrono::get_tzdb();
                            desc.*ptr = tzdb.locate_zone(row.value);                                                        
                        }
                    );
                
                } catch (const std::exception &e) {
                    throw std::runtime_error(std::format("Parse error: {} in: {} section: {}, key: {}", 
                        e.what(), file.string(), row.section, row.key));
                }

            } else if (row.section.empty() && row.key == "include") {
                walk(root/row.value, default_values);
            }
        }
    }

    std::vector<InstrumentDescription> build() {
        std::vector<InstrumentDescription> out;
        out.reserve(instruments.size());
        for (auto &[k, v]: instruments) {
            if (v.pnl_currency.id.empty()) {
                if (v.type != InstrumentType::inverse_contract) v.pnl_currency = v.quote_currency;
            }
            out.push_back(std::move(v));
        }
        instruments.clear();
        return out;
    }



};

std::vector<InstrumentDescription> configure_instruments(std::filesystem::path ini_config, 
    const InstrumentDescription &default_values, std::string_view instrument_prefix)  {

        InstrumentCollector coll(instrument_prefix);
        coll.walk(ini_config, default_values);
        return coll.build();
}


}