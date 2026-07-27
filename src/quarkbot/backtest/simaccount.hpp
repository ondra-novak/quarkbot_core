#pragma once

#include "../common/account_base.hpp"
#include <quarkbot/account.hpp>
#include <quarkbot/defs.hpp>
#include <quarkbot/underlying.hpp>
#include <quarkbot/decimal.hpp>
#include <concepts>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
namespace quarkbot {

class SimAccount final: public AccountBase, public std::enable_shared_from_this<SimAccount> {
public:


    struct WalletInfoExt : WalletInfo {
        Decimal margin_buys = {};
        Decimal margin_sells = {};        
        bool disable_balance_check = true;  //< default is true, but it is set to false if wallet is configured by constructor
    };

    SimAccount(std::string name, std::span<std::pair<UnderlyingCurrency, Decimal> > wallet);

    virtual std::string_view get_name() const override;
    virtual awaitable<WalletInfo> get_balance(UnderlyingCurrency currency) const override;
    virtual awaitable<WalletInfo> get_total_equity(UnderlyingCurrency ) const override;

    virtual awaitable<bool> transfer(UnderlyingCurrency currency, PAccount to_account, Decimal amount) override;

    static bool check_bankruptcy(const WalletInfoExt &info) ;

    ///update wallet
    template<std::invocable<WalletInfoExt &> Callback>
    bool update_wallet(const std::string &id, Callback &&cb, bool create = false) {
        auto iter = _wallet.find(id);
        if (iter == _wallet.end()) {
            if (!create) return false;
            auto ins = _wallet.emplace(id, WalletInfo{});
            iter = ins.first;
        }
        std::invoke(std::forward<Callback>(cb), iter->second);
        return check_bankruptcy(iter->second);
    }

    template<std::invocable<WalletInfoExt &> Callback>
    bool update_wallet(const UnderlyingCurrency &currency, Callback &&cb, bool create) {
        return update_wallet(currency.id, std::forward<Callback>(cb), create);
    }

    const WalletInfo &get_wallet(const std::string &id);

protected:    
    std::string _name;
    std::unordered_map<std::string, WalletInfoExt> _wallet;
};

}