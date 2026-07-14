#pragma once

#include "ipublisher.hpp"
#include "../instrument_description.hpp"
#include "../underlying.hpp"
#include "../types.hpp"
#include "quarkbot/defs.hpp"
#include "quarkbot/stream/snapshot.hpp"

namespace quarkbot {

    

class IMarketInstrument : public IPublisher{
public:

    using Info = InstrumentDescription;

    virtual ~IMarketInstrument() = default;

    virtual PExchange get_exchange() const = 0;

    virtual const Info &get_info() const = 0;
    
    ///Create tradable instrument from the instrument
    /**
      @param account associated account
      @return reference to tradable instrument, can be nullptr if not available for trading with this account
     */
    virtual PTradableInstrument create_tradable_instrument(PAccount account) = 0;

    
    virtual awaitable<bool> receive_snapshot(Snapshot &v, std::stop_token stop_token = {}) = 0;

    class Null;
    
};

class IMarketInstrument::Null final: public IMarketInstrument {
public:
    virtual PExchange get_exchange() const {throw UninitializedException();}
    virtual const Info &get_info() const  {throw UninitializedException();}
    virtual PTradableInstrument create_tradable_instrument(PAccount ) {throw UninitializedException();}
    virtual awaitable<bool> receive_snapshot(Snapshot &, std::stop_token  = {}) {throw UninitializedException();}
    virtual std::shared_ptr<IEventStreamBase> subscribe_stream(std::size_t , const void *) {return nullptr;}
};

constexpr auto null_market_instrument = IMarketInstrument::Null{};

}