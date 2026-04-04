#pragma once

#include "ifc/account.hpp"
#include "utils/decimal.hpp"
#include <optional>
#include <unordered_map>
namespace quarkbot {

class SimAccount final: public IAccount, public std::enable_shared_from_this<SimAccount> {
public:

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
            to_acc->_wallet[currency.id] = WalletInfo{amount};
        } else {
            to_iter->second.balance += amount;
        }
        return true;
    }

    void report_upnl(std::string id, Decimal upnl) {
        auto iter = _wallet.find(id);
        if (iter == _wallet.end()) return;
        iter->second.unrealized_pnl = upnl;
    }

    ///return false if wallet must be liquidated, true otherwise
    bool report_margin(std::string id, Decimal margin) {
        auto iter = _wallet.find(id);
        if (iter == _wallet.end()) return false;
        iter->second.margin = margin;
        return check_bankruptcy(iter->second);

    }

    bool check_bankruptcy(const WalletInfo &info) const {
        return info.balance + info.unrealized_pnl > info.margin + info.order_blocked;
    }

    void report_order_blocked(std::string id, Decimal order_blocked) {
        auto iter = _wallet.find(id);
        if (iter == _wallet.end()) return;
        iter->second.order_blocked = order_blocked;
    }


protected:    
    std::string _name;
    std::unordered_map<std::string, WalletInfo> _wallet;
};

}