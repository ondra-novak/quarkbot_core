#pragma once
namespace trading_api {

class SubscriptionType {
public:
    enum E : unsigned char {
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
    SubscriptionType() = default;
    SubscriptionType(E val):_val(val) {}
    bool operator==(const SubscriptionType &) const = default;
    std::strong_ordering operator<=>(const SubscriptionType &) const = default;
    E value() const {return _val;}
    template<typename T> requires(std::is_integral_v<T>)
    explicit operator T() const {return _val;}
    explicit operator std::string_view() const  {
        switch (_val) {
            case tickdata: return "L1 tick data";
            case orderbook: return "L2 orderbook";
            case auction: return "auction";
            case liquidations: return "liquidations";
            case index: return "index";
            case funding: return "funding";
            case news: return "news";
            case contracts: return "contracts";
            default: return "unknown";
        }
    }
    explicit operator std::string() const {
        return std::string(static_cast<std::string_view>(*this));
    }

    bool can_collapse() const {
        return _val == tickdata || _val == orderbook || _val == auction
            || _val == index || _val == funding; 
        
    }
    protected:
        E _val;
};


}