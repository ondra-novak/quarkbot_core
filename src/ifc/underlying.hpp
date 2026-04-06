#pragma once

#include "defs.hpp"
namespace quarkbot {


struct UnderlyingCurrency {
    ///string ID on exchange (exchange relate identifictaion, for example UST)
    std::string id;
    ///unified ID if exists, or empty. This means ISO code or well known short name (USDT for example)
    std::string unified_id;
    ///associated exchange
    PExchange exchange;

    bool operator==(const UnderlyingCurrency &) const = default;
};

}
