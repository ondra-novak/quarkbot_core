#pragma once
#include "defs.hpp"
#include "ifc/underlying.hpp"
#include <vector>


namespace quarkbot {


class IExchange {
public:
    virtual ~IExchange() = default;
    virtual PAccount create_account(const std::string &name, const std::string &credentials) const = 0;
    virtual std::vector<PMarketInstrument> get_market_instruments() const = 0;
    virtual std::vector<UnderlyingCurrency> get_all_currencies() const = 0;
    virtual std::string_view get_name() const = 0;
    
};

}