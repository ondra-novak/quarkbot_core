#include "configured_data_source.hpp"
#include "ifc/exchange.hpp"
#include "ifc/market_instrument.hpp"
#include <zlib.h>
#include <fstream>
#include <stdexcept>
#include <string>

namespace quarkbot {

IMarketInstrument::Info InstrumentSpec::resolve(IExchange &exchange) const {
    IMarketInstrument::Info info;
    info.type = type;
    info.multiplier = multiplier;
    info.tick_scale = tick_scale;
    info.name = name;
    info.quote_currency = exchange.create_currency(quote_currency);
    info.pnl_currency = exchange.create_currency(pnl_currency);
    if (asset_wallet) info.asset_wallet = exchange.create_currency(*asset_wallet);
    info.min_lot_size = min_lot_size;
    info.lot_size_increment = lot_size_increment;
    info.price_increment = price_increment;
    info.min_volume = min_volume;
    info.leverage = leverage;
    info.fee_rate_maker = fee_rate_maker;
    info.fee_rate_taker = fee_rate_taker;
    return info;
}

// Strip trailing \r\n from a string in-place
static void strip_newline(std::string &s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
}

// Parse "key=value", return false if line has no '='
static bool parse_kv(const std::string &line, std::string &key, std::string &value) {
    auto pos = line.find('=');
    if (pos == std::string::npos) return false;
    key = line.substr(0, pos);
    value = line.substr(pos + 1);
    return true;
}

void ConfiguredDataSource::parse_ini(const std::filesystem::path &ini_path) {
    std::ifstream f(ini_path);
    if (!f) throw std::runtime_error("Cannot open INI file: " + ini_path.string());

    std::string section;
    std::string line;
    std::filesystem::path source_file;

    while (std::getline(f, line)) {
        strip_newline(line);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            auto end = line.find(']');
            section = (end != std::string::npos) ? line.substr(1, end - 1) : line.substr(1);
            continue;
        }
        std::string key, val;
        if (!parse_kv(line, key, val)) continue;

        if (section == "source") {
            if (key == "file") source_file = val;
        } else if (section == "instrument") {
            if      (key == "name")               _spec.name = val;
            else if (key == "type") {
                if      (val == "spot")             _spec.type = InstrumentType::spot;
                else if (val == "contract")         _spec.type = InstrumentType::contract;
                else if (val == "inverse_contract") _spec.type = InstrumentType::inverse_contract;
            }
            else if (key == "quote_currency")     _spec.quote_currency = val;
            else if (key == "pnl_currency")       _spec.pnl_currency = val;
            else if (key == "asset_wallet")       _spec.asset_wallet = val;
            else if (key == "min_lot_size")       _spec.min_lot_size = Decimal::from_string(val);
            else if (key == "lot_size_increment") _spec.lot_size_increment = Decimal::from_string(val);
            else if (key == "price_increment")    _spec.price_increment = Decimal::from_string(val);
            else if (key == "min_volume")         _spec.min_volume = Decimal::from_string(val);
            else if (key == "leverage")           _spec.leverage = Decimal::from_string(val);
            else if (key == "fee_rate_maker")     _spec.fee_rate_maker = Decimal::from_string(val);
            else if (key == "fee_rate_taker")     _spec.fee_rate_taker = Decimal::from_string(val);
            else if (key == "multiplier")         _spec.multiplier = Decimal::from_string(val);
            else if (key == "tick_scale")         _spec.tick_scale = Decimal::from_string(val);
        }
    }

    // Validate required fields
    if (_spec.name.empty())
        throw std::runtime_error("INI missing [instrument] name=");
    if (_spec.quote_currency.empty())
        throw std::runtime_error("INI missing [instrument] quote_currency=");
    if (_spec.pnl_currency.empty())
        throw std::runtime_error("INI missing [instrument] pnl_currency=");

    // Resolve data file path: default to same stem + .csv.gz
    if (source_file.empty())
        source_file = ini_path.parent_path() / (ini_path.stem().string() + ".csv.gz");

    _gz = reinterpret_cast<gzFile_s *>(gzopen(source_file.c_str(), "rb"));
    if (!_gz)
        throw std::runtime_error("Cannot open gz file: " + source_file.string());
}

ConfiguredDataSource::ConfiguredDataSource(std::filesystem::path ini_path) {
    parse_ini(ini_path);
}

ConfiguredDataSource::~ConfiguredDataSource() {
    if (_gz) gzclose(reinterpret_cast<gzFile>(_gz));
}

std::vector<InstrumentSpec> ConfiguredDataSource::get_instrument_infos() {
    return {_spec};
}

bool ConfiguredDataSource::read_line(std::string &out) {
    out.clear();
    char buf[4096];
    while (true) {
        if (!gzgets(reinterpret_cast<gzFile>(_gz), buf, sizeof(buf)))
            return !out.empty();
        out += buf;
        // gzgets stops at newline or EOF; if we got a newline we're done
        if (!out.empty() && out.back() == '\n') {
            strip_newline(out);
            return true;
        }
        // buffer was full without a newline — loop to get rest of line
    }
}

} // namespace quarkbot
