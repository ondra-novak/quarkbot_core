#pragma once

#include "abstract/iexchange.hpp"
#include "market_instrument.hpp"
#include "quarkbot/abstract/default_shared.hpp"
#include "tradable_instrument.hpp"
#include "account.hpp"

namespace quarkbot {

class Exchange {
public:
    Exchange():_ptr(default_shared(null_exchange)) {}
    Exchange(std::shared_ptr<IExchange> state):_ptr(std::move(state)) {}
      ///create account object which is mapped to an account on the exchange. Format of credentials is exchange-specific (e.g. API key, secret, etc); for backtesting/simulation it can be left empty or used to specify initial wallet.
    Account create_account(const std::string &name, const std::string &credentials)  {
        return Account(_ptr->create_account(name, credentials));
    }
    ///get list of all instruments available on the exchange.
    std::vector<MarketInstrument> get_market_instruments() {
        return _ptr->get_market_instruments();
    }
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
    MarketInstrument create_instrument(std::string_view id, InstrumentType type)  {
        return MarketInstrument(_ptr->create_instrument(id, type));
    }
    ///query list of all currencies available on the exchange. This is used for wallet management and PnL calculations; it may or may not correspond to actual tradable assets on the exchange.
    std::vector<UnderlyingCurrency> get_all_currencies()  {
        return _ptr->get_all_currencies();
    }
    ///get exchange name. This is used for informational purposes and may be used in logging, etc.
    std::string_view get_name() const{
        return _ptr->get_name();
    }

    auto get_handle() const {return _ptr;}

protected:
    std::shared_ptr<IExchange> _ptr;
};

inline Exchange MarketInstrument::get_exchange() const {
    return Exchange(_state->get_exchange());
}
inline Exchange TradableInstrument::get_exchange() const {
        return _state->get_instrument()->get_exchange();
    }


}