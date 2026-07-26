#pragma once

#include "quarkbot/instrument_description.hpp"
#include <filesystem>
namespace quarkbot  {



    ///load instruments from config
    /**
    @param ini_config path to config
    @param default_values predefined values 
    @param instrument_prefix section prefix for instrument


    @code
        include=file.ini
        
        [instrument:BTCUSD]
        quantity_increment=1
        price_increment=0.1
        quote_currency=USD
        pnl_currency=USD
        asset_wallet=BTC
    @endcode

    */
    std::vector<InstrumentDescription> configure_instruments(std::filesystem::path ini_config, 
        const InstrumentDescription &default_values = InstrumentDescription{},
        std::string_view instrument_prefix = "instrument:");


}