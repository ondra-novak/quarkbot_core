#pragma once

#include "quarkbot/decimal.hpp"
#include "quarkbot/types.hpp"
#include "quarkbot/underlying.hpp"
#include "quarkbot/utils/lookup.hpp"
#include "quarkbot/utils/tagset.hpp"
namespace quarkbot {

enum class InstrumentCategory {
    Unknown,
    Equity,    // Stock
    Fund,      // ETF, mutual fund
    Future,    // Stock futures
    Comodity,  // comodity
    Option,    // options
    Forex,
    Crypto,
    Index,
    Bond,
    Economic, 
    Prediction  //prediction markets
};

template<>
inline constexpr auto string_lookup<InstrumentCategory> = make_string_lookup_table<InstrumentCategory>({
    {InstrumentCategory::Equity, "Equity"},
    {InstrumentCategory::Fund, "Fund"},
    {InstrumentCategory::Future, "Future"},
    {InstrumentCategory::Comodity, "Comodity"},
    {InstrumentCategory::Option, "Option"},
    {InstrumentCategory::Forex, "Forex"},
    {InstrumentCategory::Crypto, "Crypto"},
    {InstrumentCategory::Index, "Index"},
    {InstrumentCategory::Bond, "Bond"},
    {InstrumentCategory::Economic, "Economic"},
    {InstrumentCategory::Prediction, "Prediction"},
});

struct InstrumentGeometry {
    ///minimal tradable quantity
    Decimal min_quantity = {};
    ///max tradable quantity
    Decimal max_quantity = Decimal::max();
    ///quantity increment
    Decimal quantity_increment = {};
    ///price increment
    Decimal price_increment = {};
    ///minimal allowed turnover (quantity*price)
    Decimal min_turnover = {};
    ///max leverage - specify 0 for spot
    Decimal leverage = {};
    ///free for maker - (0.01 = 1%)
    Decimal fee_rate_maker = {};
    ///free for taker - (0.01 = 1%)
    Decimal fee_rate_taker = {};

};

struct InstrumentDescription : ContractInfo, InstrumentGeometry{            
    ///underlying currency for quotes
    UnderlyingCurrency quote_currency = {};
    ///underlying currency for pnl, can be different - for example inverted futures 
    UnderlyingCurrency pnl_currency = {};
    ///underlying currenct for asset if exists (nullopt for contracts, stocks and non currency assets)
    std::optional<UnderlyingCurrency> asset_wallet = {};
    ///instrument name - not need to be unique (exchange related)
    std::string name = {};
    ///Instrument category (optional description)
    InstrumentCategory category = {};
    ///various tags (optional)
    TagSet tags = {};
    ///instrument time zone - can be nullptr for UTC
    const std::chrono::time_zone *time_zone = {};
    ///instrument unique ID related to given exchange - not always used (optional)
    std::size_t uid = 0;

    ///contains class hash of extension. It allows to check that structure contains correct extension. Default value is 0, no extension
    std::size_t extension_hash = 0;
    

    ///instrument is leveraged
    bool is_leveraged() const {return leverage > 0;}
    ///there is a wallet for asset
    bool asset_has_wallet() const {return !is_leveraged() && asset_wallet.has_value();}

    Decimal calc_initial_margin(Decimal price, Decimal quantity) const {
        if (leverage) {
            return calc_turnover_pnl_currency(price, quantity) * reciprocal(leverage);
        } else {
            return 0;
        }
    }

};

}