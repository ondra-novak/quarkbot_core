
#include "../defs.hpp"
#include "../underlying.hpp"
#include "../types.hpp"

namespace quarkbot {

class IAccount {
public:

    struct WalletInfo {
        ///available balance (can be used for trading)
        Decimal balance = {};
        ///unrealized pnl for open positions in this currency (if applicable)
        Decimal unrealized_pnl = {};
        ///blocked balance for opened orders (spot markets)
        Decimal order_blocked = {};
        ///initial margin for opened orders and positions (leveraged markets)
        Decimal initial_margin = {};
        ///maintenance margin for opened positions (leveraged markets)
        Decimal maintenance_margin = {};

        Decimal remaining_balance() const {
            return balance + unrealized_pnl - order_blocked - initial_margin;
        }
    };

    virtual ~IAccount() = default;
    virtual std::string_view get_name() const = 0;
    virtual awaitable<WalletInfo> get_balance(UnderlyingCurrency currency) const = 0;
    virtual awaitable<WalletInfo> get_total_equity(UnderlyingCurrency currency) const = 0;
    virtual awaitable<bool> transfer(UnderlyingCurrency currency, std::shared_ptr<IAccount> to_account, Decimal amount)  = 0;
};  

}
