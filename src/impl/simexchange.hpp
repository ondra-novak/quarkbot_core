#pragma once


#include "ifc/defs.hpp"
#include "ifc/exchange.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/stream_defs.hpp"
#include "ifc/streaming.hpp"
#include <memory>
namespace quarkbot {

class SimExchange: public IExchange {
public:
    
    virtual PAccount create_account(const std::string &name, const std::string &credentials) const override;
    virtual std::vector<PMarketInstrument> get_market_instruments() const override;
    virtual std::vector<UnderlyingCurrency> get_all_currencies() const override;
    virtual std::string_view get_name() const override;

    std::shared_ptr<IEventStreamBase> subscribe_stream(const IMarketInstrument *instrument,const IAccount *, StreamTypeItem::Type type, const StreamParams &params);
    PTradableInstrument create_tradable_instrument(const IMarketInstrument *instrument, const IAccount *);

protected:

};


}