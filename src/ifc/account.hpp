#pragma once
#include "defs.hpp"
#include "ifc/underlying.hpp"
#include "utils/decimal.hpp"


namespace quarkbot {

class IAccount {
public:

    struct WalletInfo {
        ///available balance (can be used for trading)
        Decimal balance = {};
        ///unrealized pnl for open positions in this currency (if applicable)
        Decimal unrealized_pnl = {};
        ///margin used for open positions in this currency (if applicable)
        Decimal margin = {};
        ///amount blocked for open orders (if applicable)
        Decimal order_blocked = {};
    };

    virtual ~IAccount() = default;
    virtual std::string_view get_name() const = 0;

    ///Retrieve balance for given currency
    virtual awaitable<WalletInfo> get_balance(UnderlyingCurrency currency) const = 0;

    ///Retrieves total equity of the account
    /**
    @param currency target currency. It returns equity in selected currency
    @return WalletInfo - if currency cannot be used, returns nullopt. It is recommended to select
    quote currency or pnl currency which should be supported
     */
    virtual awaitable<WalletInfo> get_total_equity(UnderlyingCurrency currency) const = 0;

    ///transfer money from one account to other
    /**
        @param currency selected currency to transfer
        @param to_account reference to target account
        @param amount amount to transfer
        @retval true transfered
        @retval false transfer
    */  
    virtual awaitable<bool> transfer(UnderlyingCurrency currency, PAccount to_account, Decimal amount)  = 0;
};  


}
