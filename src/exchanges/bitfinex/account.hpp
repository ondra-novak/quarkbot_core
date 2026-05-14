#pragma once

#include "defs.hpp"
#include "order.hpp"
#include "signer.hpp"
#include "ifc/account.hpp"
#include <new>
namespace quarkbot {
namespace bitfinex {

class Account: public IAccount  {
public:
    virtual std::string_view get_name() const override;
    virtual awaitable<WalletInfo> get_balance(UnderlyingCurrency currency) const override;
    virtual awaitable<WalletInfo> get_total_equity(UnderlyingCurrency currency) const override;
    virtual awaitable<bool> transfer(UnderlyingCurrency currency, PAccount to_account, Decimal amount)  override;
    virtual awaitable<Position> get_position(PMarketInstrument instrument) override;


protected:
    Signer _signer;
    std::new_handler _context;

    
};


}
}