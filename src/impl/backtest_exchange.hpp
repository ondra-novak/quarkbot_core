#pragma once
#include "ifc/defs.hpp"
#include "ifc/exchange.hpp"
#include "impl/instrument_base.hpp"

#include "ifc/account.hpp"
#include <unordered_map>

namespace quarkbot {


class BacktestAccount: public IAccount {
public:
    virtual std::string_view get_name() const override;
    virtual awaitable<WalletInfo> get_balance(PUnderlyingCurrency currency) const override;
    virtual awaitable<WalletInfo> get_total_equity(PUnderlyingCurrency currency) const override;
    virtual awaitable<bool> transfer(PUnderlyingCurrency currency, PAccount to_account, Fixed amount) const override;
};

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