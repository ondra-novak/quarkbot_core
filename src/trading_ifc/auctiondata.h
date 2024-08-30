#pragma once


namespace trading_api {


class AuctionData {
public:

    enum class Type {
        unspecified,
        opening,
        closing,
        intraday,
        reopening,
        reference,
        ipo
    };


    ///timestamp of the event
    Timestamp tp = {};
    ///current indicative price
    Decimal indicative_price = {};
    ///current indicative volume
    Decimal indicative_volume = {};
    ///imbalance volume
    Decimal imbalance_volume = {};
    ///imbalance side
    Side imbalance_side = {};
    ///auction type
    Type type = {};
    ///last auction price (previous auction, if in auction now)
    Decimal last_auction_price = {};
    ///last auction volume (previous auction, if in auction now)
    Decimal last_auction_volume = {};
    ///last auction type (previous auction, if in auction now)
    Type last_auction_type = {};
    ///it is set to true, if in auction now.
    bool in_auction = false;

};


inline constexpr std::string_view to_string(AuctionData::Type t) {
    switch (t) {
        case AuctionData::Type::closing: return "closing";
        case AuctionData::Type::opening: return "opening";
        case AuctionData::Type::intraday: return "intraday";
        case AuctionData::Type::reopening: return "reopening";
        case AuctionData::Type::reference: return "reference";
        case AuctionData::Type::ipo: return "ipo";
        case AuctionData::Type::unspecified:
        default: return "unspecified";
    }
}

inline std::ostream &operator << (std::ostream &s, const AuctionData &tk) {
    if (tk.in_auction) {
        s << "ACTIVE: (" << to_string(tk.type) << " " << tk.indicative_volume << " @ " << tk.ndicative_price << ", IV: " << tk.imbalance_volume << " " << to_string(tk.imbalance_side) << ")";
    } else {
        s << "INACTIVE: " << to_string(tk.last_auction_type) << " " << tk.last_auction_volume << " @ " << tk.last_auction_price;
    }
    return s;
}

template<typename Lock = NoLock>
using MarketEvent_AuctionData = MarketEventHolder<MarketEventType::auction, AuctionData, Lock>;


}
