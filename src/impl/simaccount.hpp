#pragma once

#include "ifc/account.hpp"
#include "ifc/defs.hpp"
#include "ifc/underlying.hpp"
#include "utils/decimal.hpp"
#include <concepts>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
namespace quarkbot {

class SimAccount final: public IAccount, public std::enable_shared_from_this<SimAccount> {
public:



    struct WalletInfoExt : WalletInfo {
        Decimal margin_buys = {};
        Decimal margin_sells = {};
    };

    SimAccount(std::string name, std::span<std::pair<UnderlyingCurrency, Decimal> > wallet)
        :_name(std::move(name))
    {
        for (auto &x: wallet) {
            update_wallet(x.first,[&](WalletInfoExt &w){w.balance = x.second;},true);
        }
    }


    virtual std::string_view get_name() const override {
        return _name;
    }
    virtual awaitable<WalletInfo> get_balance(UnderlyingCurrency currency) const override {
        auto iter = _wallet.find(currency.id);
        if (iter == _wallet.end()) return std::nullopt;
        return iter->second;
    }

    virtual awaitable<WalletInfo> get_total_equity(UnderlyingCurrency ) const override {return std::nullopt;}


    virtual awaitable<bool> transfer(UnderlyingCurrency currency, PAccount to_account, Decimal amount) override {
        auto iter = _wallet.find(currency.id);
        if (iter == _wallet.end()) return false;
        if (iter->second.balance < amount) return false;
        auto to_acc = std::dynamic_pointer_cast<SimAccount>(to_account);
        if (!to_acc) return false;
        iter->second.balance -= amount;
        auto to_iter = to_acc->_wallet.find(currency.id);
        if (to_iter == to_acc->_wallet.end()) {
            to_acc->_wallet[currency.id] = WalletInfoExt{{amount}};
        } else {
            to_iter->second.balance += amount;
        }
        return true;
    }

    bool check_bankruptcy(const WalletInfo &info) const {
        return info.balance + info.unrealized_pnl > info.initial_margin + info.order_blocked;
    }



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

    const WalletInfo &get_wallet(const std::string &id) {
        static WalletInfo empty;
        auto iter = _wallet.find(id);
        if (iter == _wallet.end()) return empty;
        return iter->second;
    }

protected:    
    std::string _name;
    std::unordered_map<std::string, WalletInfoExt> _wallet;
};

}