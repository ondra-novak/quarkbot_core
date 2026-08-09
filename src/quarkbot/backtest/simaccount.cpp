#include "simaccount.hpp"

namespace quarkbot {

    SimAccount::SimAccount(std::string name, std::span<std::pair<UnderlyingCurrency, Decimal> > wallet)
        :_name(std::move(name))
    {
        for (auto &x: wallet) {
            update_wallet(x.first,[&](WalletInfoExt &w){
                w.balance = x.second;
                w.disable_balance_check = false;
            },true);
        }
    }


    std::string_view SimAccount::get_name() const {
        return _name;
    }
    awaitable<WalletInfo> SimAccount::get_balance(UnderlyingCurrency currency) const {
        auto iter = _wallet.find(currency.id);
        if (iter == _wallet.end()) return std::nullopt;
        return iter->second;
    }

    awaitable<WalletInfo> SimAccount::get_total_equity(UnderlyingCurrency ) const {return std::nullopt;}


    awaitable<bool> SimAccount::transfer(UnderlyingCurrency currency, PAccount to_account, Decimal amount) {
        auto iter = _wallet.find(currency.id);
        if (iter == _wallet.end()) return false;        
        if (iter->second.balance < amount) return false;
        auto to_acc = std::dynamic_pointer_cast<SimAccount>(to_account);
        if (!to_acc) return false;
        iter->second.balance -= amount;
        if (check_bankruptcy(iter->second)) {
            iter->second.balance+=amount;
            return false;
        }
        auto to_iter = to_acc->_wallet.find(currency.id);
        if (to_iter == to_acc->_wallet.end()) {
            to_acc->_wallet[currency.id] = WalletInfoExt{{amount}};
        } else {
            to_iter->second.balance += amount;
        }
        return true;
    }

    bool SimAccount::check_bankruptcy(const WalletInfoExt &info)  {
        if (info.disable_balance_check) return false;
        return info.balance + info.unrealized_pnl < info.initial_margin + info.order_blocked;
    }



       const WalletInfo &SimAccount::get_wallet(const std::string &id) {
        static WalletInfo empty;
        auto iter = _wallet.find(id);
        if (iter == _wallet.end()) return empty;
        return iter->second;
    }


}