
#pragma once
#include "../defs.hpp"
#include "../underlying.hpp"
#include "../types.hpp"
#include "quarkbot/abstract/iriskcontroller.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/risk_controller.hpp"

namespace quarkbot {

class IAccount: public IRiskControl {
public:

    using WalletInfo = ::quarkbot:: WalletInfo;

    virtual ~IAccount() = default;
    virtual std::string_view get_name() const = 0;
    virtual awaitable<WalletInfo> get_balance(UnderlyingCurrency currency) const = 0;
    virtual awaitable<WalletInfo> get_total_equity(UnderlyingCurrency currency) const = 0;
    virtual awaitable<bool> transfer(UnderlyingCurrency currency, std::shared_ptr<IAccount> to_account, Decimal amount)  = 0;
    virtual RiskController set_risk_controller(RiskController) =0;

    class Null;
};  


class IAccount::Null final: public IAccount {
public:
    virtual std::string_view get_name() const override {return {"<null>"};}
    virtual awaitable<WalletInfo> get_balance(UnderlyingCurrency ) const override {return {};}
    virtual awaitable<WalletInfo> get_total_equity(UnderlyingCurrency ) const override {return {};}
    virtual awaitable<bool> transfer(UnderlyingCurrency , std::shared_ptr<IAccount> , Decimal ) override {return false;}
    virtual RiskController set_risk_controller(RiskController) override {return {};}
    virtual CheckResult pre_trade_check(const Order &) override {return {false, OrderRejectionReason::not_tradable};}
    virtual void on_order_event(const Order &, const Fill &) override {}
    virtual void on_order_event(const Order &, OrderStatus ) override {};
};


}
