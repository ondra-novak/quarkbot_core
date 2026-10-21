#pragma once

#include "../types.hpp"
#include "trade.hpp"


namespace quarkbot {

///Calculates statistics between two intervals
struct TradeStatCounter {
    
    struct MarketInstrumentStream {};
    /// total count of trades
    std::uint64_t trades = 0;
    /// total volume - it can reset when stream is reopened
    Decimal volume ={};
    /// total buy volume - exchange must report side
    Decimal buy_volume = {};
    /// total sell volume  - exchange must report side
    Decimal sell_volume = {};
    /// last price
    Decimal last_price = {};
    /// weighted price sum
    Decimal weighted_price_sum = {};
    /// weighted price sq sum
    Decimal weighted_price_sq_sum = {};    
    /// time of last even
    std::chrono::system_clock::time_point time;
    /// total count of auction trades - a Trade can be missed, this can't
    /** a venue can report one auction as several prints (multiple market centers) */
    std::uint64_t auctions = 0;
    /// total volume of auction trades
    Decimal auction_volume = {};
    /// total turnover (price * volume) of auction trades
    Decimal auction_turnover = {};
    /// time of last auction trade
    std::chrono::system_clock::time_point last_auction_time;
    /// type of last auction trade, unknown before the first one
    AuctionType last_auction_type = AuctionType::unknown;

    ///calculate count of trades between two samples
    friend std::uint64_t delta_trades(const TradeStatCounter &beg, const TradeStatCounter &end) {
        return end.trades - beg.trades;
    }
    ///calculate volume between two samples
    friend Decimal delta_volume(const TradeStatCounter &beg, const TradeStatCounter &end) {
        return end.volume - beg.volume;
    }
    ///calculate buy volume between two samples
    friend Decimal delta_buy_volume(const TradeStatCounter &beg, const TradeStatCounter &end) {
        return end.buy_volume - beg.buy_volume;
    }
    ///calculate sell volume between two samples
    friend Decimal delta_sell_volume(const TradeStatCounter &beg, const TradeStatCounter &end) {
        return end.sell_volume - beg.sell_volume;
    }
    ///calculate volume weighted average price
    friend Decimal vwap(const TradeStatCounter &beg, const TradeStatCounter &end) {
        Decimal vol = delta_volume(beg, end);        
        return vol?(end.weighted_price_sum - beg.weighted_price_sum)/vol: 0_dec;
    }
    ///calculate price variance
    friend Decimal variance(const TradeStatCounter &beg, const TradeStatCounter &end) {
        Decimal vol = delta_volume(beg, end);        
        if (!vol) return vol;
        Decimal avg = (end.weighted_price_sum - beg.weighted_price_sum)/vol;
        Decimal avg2 = (end.weighted_price_sq_sum - beg.weighted_price_sq_sum)/vol;
        return  avg2 - avg*avg;
    }   

    ///calculate count of auction trades between two samples
    friend std::uint64_t delta_auctions(const TradeStatCounter &beg, const TradeStatCounter &end) {
        return end.auctions - beg.auctions;
    }
    ///calculate auction volume between two samples
    friend Decimal delta_auction_volume(const TradeStatCounter &beg, const TradeStatCounter &end) {
        return end.auction_volume - beg.auction_volume;
    }
    ///calculate volume weighted average price of auctions between two samples
    /** with a single auction between the samples it is the auction price */
    friend Decimal auction_vwap(const TradeStatCounter &beg, const TradeStatCounter &end) {
        Decimal vol = delta_auction_volume(beg, end);
        return vol?(end.auction_turnover - beg.auction_turnover)/vol: 0_dec;
    }

    ///update statistics adding one trade
    /**
    It is recommended to process trades in chronological order
    Function returns new data point (original data point remains untouched)
     */
    TradeStatCounter add(const Trade &tr) const {
        bool auction = is_auction_trade(tr.qualifier);
        return {
            trades+1,
            volume + tr.size,
            buy_volume + (tr.side == Side::buy?tr.size:0),
            sell_volume + (tr.side == Side::sell?tr.size:0),
            tr.price,
            weighted_price_sum+tr.price*tr.size,
            weighted_price_sq_sum + tr.price*tr.price*tr.size,
            tr.time,
            auctions + (auction?1:0),
            auction_volume + (auction?tr.size:0),
            auction_turnover + (auction?tr.price*tr.size:0),
            auction?tr.time:last_auction_time,
            auction?to_auction_type(tr.qualifier):last_auction_type};

    }
};

///Old name
using TradeCounter = TradeStatCounter;

}