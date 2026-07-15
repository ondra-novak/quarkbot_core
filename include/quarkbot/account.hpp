#pragma once
#include "abstract/iaccount.hpp"
#include "utils/wrapper.hpp"

namespace quarkbot {

class Account : public Wrapper<IAccount>{
public:
    using WalletInfo = IAccount::WalletInfo;
    using Wrapper<IAccount>::Wrapper;

    std::string_view get_name() const {return _ptr->get_name();}
    
    ///Retrieve balance for given currency
    awaitable<WalletInfo> get_balance(UnderlyingCurrency currency) const {return _ptr->get_balance(std::move(currency));}
    
    ///Retrieves total equity of the account
    /**
    @param currency target currency. It returns equity in selected currency
    @return WalletInfo - if currency cannot be used, returns nullopt. It is recommended to select
    quote currency or pnl currency which should be supported
     */
    awaitable<WalletInfo> get_total_equity(UnderlyingCurrency currency) const  {return _ptr->get_total_equity(std::move(currency));};

    ///transfer money from one account to other
    /**
        @param currency selected currency to transfer
        @param to_account reference to target account
        @param amount amount to transfer
        @retval true transfered
        @retval false transfer
    */  
    awaitable<bool> transfer(UnderlyingCurrency currency, const Account &to_account, Decimal amount) const {
            return _ptr->transfer(std::move(currency), to_account.get_handle(), amount);}

    ///Sets new risk controller
    /**
        @param new_controller new controller
        @return previous controller

        @note It is expected that function is MT safe. However it is recommended to set this controller as the first 
        operation before trading is started, so before the first order is placed. Changing controller in
        middle of trading can corrupt state of the controller itself
    */
    RiskController set_risk_controller(RiskController new_controller)  {
        return _ptr->set_risk_controller(std::move(new_controller));
    }


};




}
