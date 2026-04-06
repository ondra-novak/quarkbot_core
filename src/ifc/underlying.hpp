#pragma once

#include "defs.hpp"
namespace quarkbot {


struct UnderlyingCurrency {
    ///string ID on exchange (exchange relate identifictaion, for example UST)
    std::string id;
    ///unified ID if exists, or empty. This means ISO code or well known short name (USDT for example)
    std::string unified_id;
    ///associated exchange - use this pointer only as identification of the exchange
    const IExchange *exchange;

    bool operator==(const UnderlyingCurrency &) const = default;

    struct Hash {std::size_t operator()(const UnderlyingCurrency &x) const {
        std::hash<std::string> hasher;
        std::hash<const IExchange *> hasher_ptr;
        return hasher(x.id) + hasher(x.unified_id) + hasher_ptr(x.exchange);
    }};
};

}
