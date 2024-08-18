#pragma once
namespace trading_api {

enum class SubscriptionType {
    ///tick data (L1)
    tickdata,
    ///full orderbook
    orderbook,
    ///(not supported yet) auction data (Stocks)
    auction,
    ///(not supported yet) liquidation stream
    liquidations,
    ///(not supported yet) underlying index
    index,
    ///(not supported yet) perpetual contract funding
    funding,
    ///(not supported yet) instrument news
    news,
    ///(not supported yet) contract expiration, related contract listing
    contracts
};



inline std::string_view to_string(SubscriptionType type) {
    switch (type) {
        case SubscriptionType::tickdata: return "L1 tick data";
        case SubscriptionType::orderbook: return "L2 orderbook";
        case SubscriptionType::auction: return "auction";
        case SubscriptionType::liquidations: return "liquidations";
        case SubscriptionType::index: return "index";
        case SubscriptionType::funding: return "funding";
        case SubscriptionType::news: return "news";
        case SubscriptionType::contracts: return "contracts";
        default: return "unknown";
    }
}


}
