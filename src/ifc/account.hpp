#pragma once
#include "abstract/iaccount.hpp"

namespace quarkbot {

class Account {
public:
    using WalletInfo = IAccount::WalletInfo;
    Account(std::shared_ptr<IAccount> state):_state(state) {}

    std::string_view get_name() const {return _state->get_name();}
    
    ///Retrieve balance for given currency
    awaitable<WalletInfo> get_balance(UnderlyingCurrency currency) const {return _state->get_balance(std::move(currency));}
    
    ///Retrieves total equity of the account
    /**
    @param currency target currency. It returns equity in selected currency
    @return WalletInfo - if currency cannot be used, returns nullopt. It is recommended to select
    quote currency or pnl currency which should be supported
     */
    awaitable<WalletInfo> get_total_equity(UnderlyingCurrency currency) const  {return _state->get_total_equity(std::move(currency));};

    ///transfer money from one account to other
    /**
        @param currency selected currency to transfer
        @param to_account reference to target account
        @param amount amount to transfer
        @retval true transfered
        @retval false transfer
    */  
    awaitable<bool> transfer(UnderlyingCurrency currency, const Account &to_account, Decimal amount) const {
            return _state->transfer(std::move(currency), to_account._state, amount);}

    auto get_handle() const {return _state;}

    bool operator==(const Account &) const = default;

protected:
    std::shared_ptr<IAccount> _state;
};


}
