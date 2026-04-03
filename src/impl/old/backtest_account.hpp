
#include "ifc/account.hpp"

namespace quarkbot {


class BacktestAccount: public IAccount {
public:
    virtual std::string_view get_name() const override;
    virtual awaitable<WalletInfo> get_balance(PUnderlyingCurrency currency) const override;
    virtual awaitable<WalletInfo> get_total_equity(PUnderlyingCurrency currency) const override;
    virtual awaitable<bool> transfer(PUnderlyingCurrency currency, PAccount to_account, Fixed amount) const override;
protected:
    std::string _name;
};

}