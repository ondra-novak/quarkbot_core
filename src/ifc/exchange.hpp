#pragma once
#include "defs.hpp"
#include "ifc/types.hpp"
#include "ifc/underlying.hpp"
#include <string_view>
#include <vector>

namespace quarkbot {


///Interface for an exchange. This is the main entry point for users of the library; all interactions with the exchange are done through this interface and objects obtained from it.
class IExchange {
public:
    virtual ~IExchange() = default;
    ///create account object which is mapped to an account on the exchange. Format of credentials is exchange-specific (e.g. API key, secret, etc); for backtesting/simulation it can be left empty or used to specify initial wallet.
    virtual PAccount create_account(const std::string &name, const std::string &credentials)  = 0;
    ///get list of all instruments available on the exchange.
    virtual std::vector<PMarketInstrument> get_market_instruments()  = 0;
    ///Create instrument object by id. Id format is exchange-specific (e.g. symbol, contract code, etc).
    /**
    @param id Instrument identifier (e.g. symbol, contract code, etc). Format is exchange-specific.
    @param type Instrument type (e.g. spot, contract, inverse contract). 
    @return Instrument object corresponding to the given id. Throws an exception if the instrument is not found. 
            Function never return nullptr

    @note There is only one instance of an instrument object for a given id; multiple calls to this function with the same id will return the same object.
          Internally instruments are held as weak_refs, so if all references to an instrument are released, it can be destroyed and recreated on the next call to this function. 
          However, as long as there is at least one reference to an instrument, it will not be destroyed and the same object will be returned.
     */
    virtual PMarketInstrument create_instrument(std::string_view id, InstrumentType type)  = 0;
    ///query list of all currencies available on the exchange. This is used for wallet management and PnL calculations; it may or may not correspond to actual tradable assets on the exchange.
    virtual std::vector<UnderlyingCurrency> get_all_currencies()  = 0;
    ///get exchange name. This is used for informational purposes and may be used in logging, etc.
    virtual std::string_view get_name() const = 0;
    


};

} // namespace quarkbot
