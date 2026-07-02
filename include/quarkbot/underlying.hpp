#pragma once

#include "defs.hpp"
namespace quarkbot {


struct UnderlyingCurrency {
    ///string ID on exchange (exchange relate identifictaion, for example UST)
    std::string id;
    ///unified ID if exists, or empty. This means ISO code or well known short name (USDT for example)
    std::string unified_id = {};
    ///associated exchange - use this pointer only as identification of the exchange
    const IExchange *exchange = {};

    bool operator==(const UnderlyingCurrency &) const = default;

    std::size_t get_hash() const {
        std::hash<std::string> hasher;
        std::hash<const IExchange *> hasher_ptr;
        return hasher(id) + hasher(unified_id) + hasher_ptr(exchange);
    }
};

}
