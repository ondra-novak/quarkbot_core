#pragma once

#include "../stream_defs.hpp"
#include "../types.hpp"
#include "trade.hpp"


namespace quarkbot {

///Calculates statistics between two intervals
struct TradeStatCounter : public MarketInstrumentStreamTypeItem {
    
    static constexpr Type type = "trade_stats";
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

    ///update statistics adding one trade
    /**
    It is recommended to process trades in chronological order
    Function returns new data point (original data point remains untouched)
     */
    TradeStatCounter add(const Trade &tr) const {
        return {
            *this,
            trades+1,
            volume + tr.size,
            buy_volume + (tr.side == Side::buy?tr.size:0),
            sell_volume + (tr.side == Side::sell?tr.size:0),
            tr.price,
            weighted_price_sum+tr.price*tr.size,
            weighted_price_sq_sum + tr.price*tr.price*tr.size,
            tr.time};

    }
};

///Old name
using TradeCounter = TradeStatCounter;

}