#pragma once
#include "ifc/defs.hpp"
#include "ifc/exchange.hpp"
#include "ifc/market_events.hpp"
#include "ifc/market_instrument.hpp"
#include "impl/instrument_base.hpp"
#include "impl/stream_impl.hpp"
#include <memory>
#include <unordered_map>

namespace quarkbot {

class BacktestExchange: public IExchange {
public:

    virtual PAccount create_account(const std::string &, const std::string &) const override {return {};}    
    virtual std::vector<PMarketInstrument> get_market_instruments() const override;
    virtual std::vector<PUnderlyingCurrency> get_all_iso_currencies() const override;
    virtual PUnderlyingCurrency query_currency(const std::string &query) const override;
    virtual std::string_view get_name() const override {return "backtest";}

    class Instrument: public InstrumentBase {
    public:
        using InstrumentBase::InstrumentBase;
    };


protected:
    std::unordered_map<std::string, PMarketInstrument> instruments;
    
    



};


}