#pragma once

#include "defs.hpp"
#include "ifc/stream_defs.hpp"
#include "ifc/underlying.hpp"
#include "types.hpp"
#include "ifc/streaming.hpp"
#include "utils/decimal.hpp"
#include <chrono>
#include <memory>
namespace quarkbot {




class IMarketInstrument : public IPublisher<MarketInstrumentStreamTypeItem>{
public:
    struct Info : ContractInfo {
        Decimal min_lot_size = {};
        Decimal max_lot_size = Decimal::max();
        Decimal lot_size_increment = {};
        Decimal price_increment = {};
        Decimal min_volume = {};
        Decimal leverage = {};      //0 used for spot
        Decimal fee_rate_maker = {};
        Decimal fee_rate_taker = {};
        ///underlying currency for quotes
        UnderlyingCurrency quote_currency;
        ///underlying currency for pnl, can be different - for example inverted futures 
        UnderlyingCurrency pnl_currency;
        ///underlying currenct for asset if exists (nullopt for contracts, stocks and non currency assets)
        std::optional<UnderlyingCurrency> asset_wallet;
        ///instrument name - not need to be unique (exchange related)
        std::string name;

        ///instrument is leveraged
        bool is_leveraged() const {return leverage > 0;}
        ///there is a wallet for asset
        bool asset_has_wallet() const {return !is_leveraged() && asset_wallet.has_value();}

        Decimal calc_initial_margin(Decimal price, Decimal quantity) const {
            if (leverage) {
                return calc_turnover_pnl_currency(price, quantity) * reciprocal(leverage);
            } else {
                return 0;
            }
        }
    
    };
    virtual ~IMarketInstrument() = default;

    virtual PExchange get_exchange() const = 0;

    virtual const Info &get_info() const = 0;
    
    ///Create tradable instrument from the instrument
    /**
      @param account associated account
      @return reference to tradable instrument, can be nullptr if not available for trading with this account
     */
    virtual awaitable<PTradableInstrument> create_tradable_instrument(PAccount account) = 0;

};




struct Quote : MarketInstrumentStreamTypeItem {
    Decimal bid;
    Decimal bid_size;
    Decimal ask;
    Decimal ask_size;
    std::chrono::system_clock::time_point time;
    static constexpr Type type = "quote";
    Quote &view() {return *this;}

    bool operator==(const Quote &) const  = default;
};

struct Trade : MarketInstrumentStreamTypeItem {
    ///trade price
    Decimal price;
    ///trade volume
    Decimal size;
    ///time of execution
    std::chrono::system_clock::time_point time;
    ///taker's side - this is optional - exchange don't need to report side
    Side side  = Side::undetermined;
    Trade &view() {return *this;}
    static constexpr Type type = "trade";

    bool operator==(const Trade &) const  = default;
};

struct OrderBookLevel {
    Decimal price = {}; // price level
    Decimal size = {};  // new size (if <= 0 then remove the level)
};

struct OrderBookIncrement : OrderBookLevel, MarketInstrumentStreamTypeItem {
    Side side = {};
    std::chrono::system_clock::time_point time;

    OrderBookIncrement &view() {return *this;}
    static constexpr Type type = "orderbook_increment";
};


template<typename X>
struct StreamSingleParam: StreamParams {
    X param;
};

template<typename X, typename Y>
struct StreamDoubleParam: StreamParams {
    X param1;
    Y param2;
};

struct ClosedBar  {
    Decimal open = 0;
    Decimal high = 0;
    Decimal low = 0;
    Decimal close = 0;
    Decimal volume = 0; //volume is optional, if not available, it is set to zero
    std::size_t trades = 0; //count of trades. Should be at least 1 otherwise structure is not valid. Value 1 means that no informations about trades available
    std::size_t interval_index = 0;
    ClosedBar &view() {return *this;}
    static constexpr MarketInstrumentStreamTypeItem::Type type = "closed_bar";
    using ParamType =  StreamSingleParam<unsigned int>;
    static std::size_t to_interval_index(std::chrono::system_clock::time_point tp, unsigned int interval_sec) {
        return static_cast<std::size_t>(std::chrono::system_clock::to_time_t(tp)/interval_sec);
    }
};

template<unsigned int _interval_sec>
struct ClosedBarInterval: ClosedBar, MarketInstrumentStreamTypeItem{
    constexpr static auto params =ParamType {{},_interval_sec};
    std::chrono::system_clock::time_point interval_begin() const {
        return std::chrono::system_clock::from_time_t(static_cast<time_t>(interval_index * _interval_sec));        
    }
    std::chrono::system_clock::time_point interval_end() const {
        return std::chrono::system_clock::from_time_t(static_cast<time_t>(interval_index * _interval_sec+1));
    }
};

struct DecimalRange {
    uint64_t encoded;
    constexpr DecimalRange(Decimal val):encoded(std::bit_cast<uint64_t>(val)) {}
    constexpr Decimal as_decimal() const {return std::bit_cast<Decimal>(encoded);  }
};

struct RangeBarView: MarketInstrumentStreamTypeItem {
    static constexpr MarketInstrumentStreamTypeItem::Type type = "ranged_bar";
    using ParamType =  StreamSingleParam<Decimal>;
    Decimal open = 0;
    Decimal high = 0;
    Decimal low = 0;
    Decimal close = 0;
    Decimal volume = 0; //volume is optional, if not available, it is set to zero

    /**
    gap indication. This indicates, that current tick was too far from the previous, 
    which would otherwise create virtual candles. As the virtual candles creation 
    isn't  supported. reader should create them by own if they need them.

    When gap is true, (close - high) or (low - close) is larger than specified range.
    The actual value of close is (last - range) or (last + range) (depends on direction)
    The new candle is opened 
     */
    bool gap = false; 
    RangeBarView &view() {return *this;}
    std::chrono::system_clock::time_point open_tp = {};
    std::chrono::system_clock::time_point close_tp = {};
};

template<DecimalRange range>
struct RangedBar : RangeBarView {
    constexpr static auto params = ParamType{{}, range.as_decimal()};
};


struct OrderBookView {
    std::span<OrderBookLevel> bids = {};
    std::span<OrderBookLevel> asks = {};
    std::chrono::system_clock::time_point time;

    OrderBookView() = default;
    OrderBookView(std::span<OrderBookLevel> bids,std::span<OrderBookLevel> asks,std::chrono::system_clock::time_point time)
        :bids(bids),asks(asks),time(time) {}

    OrderBookView &operator=(const OrderBookView &other) noexcept{
        if (this != &other) {
            auto dbids = std::min(bids.size(), other.bids.size());
            auto dasks = std::min(asks.size(), other.asks.size());
            std::copy_n(other.bids.begin(), dbids, bids.begin());
            std::copy_n(other.asks.begin(), dasks, asks.begin());
            time = other.time;
        }        
        return *this;
    }
};


template<unsigned int depth>
struct OrderBook: MarketInstrumentStreamTypeItem {
public:
    constexpr static auto params =StreamSingleParam<unsigned int> {{},depth};
    std::chrono::system_clock::time_point time;
    std::array<OrderBookLevel,depth> bids; 
    std::array<OrderBookLevel,depth> asks;     

    OrderBookView view() {return OrderBookView(bids,asks,&time);}

    static constexpr Type type = "orderbook_snapshot";
};

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
    friend Decimal delta_trades(const TradeStatCounter &beg, const TradeStatCounter &end) {
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
            {},
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

struct PeriodicSnapshotView : public Quote{
public:    
    static constexpr Type type = "periodic_snapshot";
    Decimal last_price;
    PeriodicSnapshotView &view() {return *this;}
    using ParamType =  StreamSingleParam<unsigned int>;
};

///Stream with snapshot
/**
    The stream produces value in specified interval . The adapter chooses the best approach to obtain these values.
    For short periods, it can subscribe a stream
    For long periords, it can use REST request for ticker value
    
 */
template<unsigned interval>
requires(interval >= 1)
struct PeriodicSnapshot : PeriodicSnapshotView{
    constexpr static auto params =ParamType{{},interval};
};

///Stream is updated when some informations about instrument changed
struct InstrumentInfo : public MarketInstrumentStreamTypeItem {
    static constexpr Type type = "instrument_info";
        ///new min lot size        
        Decimal min_lot_size = {};
        ///new lot increment
        Decimal lot_size_increment = {};
        ///new price increment
        Decimal price_increment = {};
        ///new min volume
        Decimal min_volume = {};
        ///new leverage
        Decimal leverage = {};      //0 used for spot
        ///new fee rate maker    
        Decimal fee_rate_maker = {};
        ///new fee rate taker
        Decimal fee_rate_taker = {};
        ///new multiplier
        Decimal multiplier = {};
        ///new tick_scale
        Decimal tick_scale = {};

        auto &view() {return *this;}

        ///create this object from instrument information
        static InstrumentInfo from(IMarketInstrument::Info nfo) {
            return {{},
                nfo.min_lot_size,
                nfo.lot_size_increment,
                nfo.price_increment,
                nfo.min_volume,
                nfo.leverage,
                nfo.fee_rate_maker,
                nfo.fee_rate_taker,
                nfo.multiplier,
                nfo.tick_scale
            };
        }

        ///apply this object to extisting info object
        void apply(IMarketInstrument::Info &nfo) const {
                nfo.min_lot_size = min_lot_size;
                nfo.lot_size_increment = lot_size_increment;
                nfo.price_increment = price_increment;
                nfo.min_volume = min_volume;
                nfo.leverage = leverage;
                nfo.fee_rate_maker = fee_rate_maker;
                nfo.fee_rate_taker = fee_rate_taker;
                nfo.multiplier = multiplier;
                nfo.tick_scale = tick_scale;
        }
    };

}