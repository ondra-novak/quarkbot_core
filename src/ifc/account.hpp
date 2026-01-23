#pragma once
#include "defs.hpp"
#include <vector>

namespace quarkbot {

class IAccount {
public:

    struct WalletInfo {
        PUnderlyingCurrency currency;
        double balance = 0.0;
        double available = 0.0;
    };

    virtual ~IAccount() = default;
    virtual std::vector<PTradableInstrument> get_tradable_instruments() = 0;
    virtual std::vector<WalletInfo> get_wallets() = 0;
    virtual WalletInfo get_margin_equity() const = 0;

    virtual std::string_view get_name() const = 0;

};


}
