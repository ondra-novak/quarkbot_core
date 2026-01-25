#pragma once
#include "defs.hpp"
#include <vector>


namespace quarkbot {


class IExchange {
public:
    virtual ~IExchange() = default;
    virtual PAccount create_account(const std::string &name, const std::string &credentials) const = 0;
    virtual std::vector<PMarketInstrument> get_market_instruments() const = 0;
    /**    
    * @return All ISO currencies supported by this exchange. There must exist conversion between
    * these currencies and all tradable instruments on this exchange. For example USD, EUR, etc
    */
    virtual std::vector<PUnderlyingCurrency> get_all_iso_currencies() const = 0;

    ///Query for underlying currency by unique id or symbol. 
    /**
     @param query The query interpreted by exchange to find the currency. It can be unique id or symbol depending on exchange.  

     For example "USD" or "BTC" will probably return USD or Bitcoin currency object valid for this exchange. 
     However on some exchanges, different names can be used for same currency (for example "XBT" instead of "BTC"), so query should be interpreted by exchange implementation.
     @return Pointer to currency object or nullptr if not found.
     */
    virtual PUnderlyingCurrency query_currency(const std::string &query) const = 0;

    virtual std::string_view get_name() const = 0;
    
};

}