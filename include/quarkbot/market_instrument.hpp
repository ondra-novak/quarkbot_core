#pragma once

#include "abstract/imarket_instrument.hpp"
#include "abstract/ipublisher.hpp"
#include "quarkbot/instrument_description.hpp"
#include "stream_defs.hpp"
#include <concepts>
namespace quarkbot {


class Account;
class TradableInstrument;

///Market instrument represents financial instrument. It only provides information about the instrument and allows to subscribe to its streams.
/**
    You cannot use this to place orders, for that you need to create TradableInstrument by calling create_tradable_instrument() with account as parameter.
*/
class MarketInstrument {
public:

    using Info = IMarketInstrument::Info;

    ///Get exchange associated with this instrument
    PExchange get_exchange() const {return _state->get_exchange();}
    ///Get information about this instrument
    const Info &get_info() const {return _state->get_info();}

    ///Create tradable instrument for this market instrument and given account.
    TradableInstrument create_tradable_instrument(const Account &account) const;


    template<std::derived_from<MarketInstrumentStreamTypeItem> T>
    requires(StreamWithoutParam<T> || StreamWithConstantParam<T>)
    EventStream<T> subscribe() {
        return _state->subscribe<T>();
    }

    template<std::derived_from<MarketInstrumentStreamTypeItem> T>
    requires(StreamWithParam<T>)
    EventStream<T> subscribe(typename T::Params params) {
        return _state->subscribe<T>(params);
    }

    MarketInstrument(std::shared_ptr<IMarketInstrument> state):_state(std::move(state)){}
    
    bool operator==(const MarketInstrument &) const = default;

    auto get_handle() const {return _state;}

protected:
    std::shared_ptr<IMarketInstrument> _state;
};


///Stream is updated when some informations about instrument changed
struct InstrumentInfo : public MarketInstrumentStreamTypeItem, public InstrumentGeometry  {

};

}