#include "configured_data_source.hpp"
#include "ifc/exchange.hpp"
#include "ifc/market_instrument.hpp"
#include <stdexcept>

namespace quarkbot {

IMarketInstrument::Info InstrumentSpec::resolve(IExchange &exchange) const {
    IMarketInstrument::Info info;
    static_cast<ContractInfo &>(info).type = type;
    static_cast<ContractInfo &>(info).multiplier = multiplier;
    static_cast<ContractInfo &>(info).tick_scale = tick_scale;
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

ConfiguredDataSource::ConfiguredDataSource(std::filesystem::path) {
    throw std::logic_error("ConfiguredDataSource: not yet implemented");
}

ConfiguredDataSource::~ConfiguredDataSource() {}

std::vector<InstrumentSpec> ConfiguredDataSource::get_instrument_infos() {
    return {_spec};
}

bool ConfiguredDataSource::read_line(std::string &) {
    return false;
}

} // namespace quarkbot
