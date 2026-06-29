#pragma once

#include "../underlying.hpp"
#include "../types.hpp"
#include "../abstract/ipublisher.hpp"
#include "ifc/config.hpp"
#include <functional>
#include <type_traits>
namespace quarkbot {


class IMarketInstrument : public IPublisher{
public:
    struct Geometry {
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
    struct Info : ContractInfo, Geometry{            
        ///underlying currency for quotes
        UnderlyingCurrency quote_currency = {};
        ///underlying currency for pnl, can be different - for example inverted futures 
        UnderlyingCurrency pnl_currency = {};
        ///underlying currenct for asset if exists (nullopt for contracts, stocks and non currency assets)
        std::optional<UnderlyingCurrency> asset_wallet = {};
        ///instrument name - not need to be unique (exchange related)
        std::string name = {};

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
    virtual ~IMarketInstrument() = default;

    virtual PExchange get_exchange() const = 0;

    virtual const Info &get_info() const = 0;
    
    ///Create tradable instrument from the instrument
    /**
      @param account associated account
      @return reference to tradable instrument, can be nullptr if not available for trading with this account
     */
    virtual PTradableInstrument create_tradable_instrument(PAccount account) = 0;
};
}