#pragma once

#include "defs.hpp"
#include "ifc/stream_defs.hpp"
#include "ifc/underlying.hpp"
#include "types.hpp"
#include "ifc/streaming.hpp"
#include "utils/decimal.hpp"
#include <memory>
namespace quarkbot {




class IMarketInstrument {
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

    ///Internal
    virtual std::unique_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams *params) = 0;
    
    ///Create tradable instrument from the instrument
    /**
      @param account associated account
      @return reference to tradable instrument, can be nullptr if not available for trading with this account
     */
    virtual awaitable<PTradableInstrument> create_tradable_instrument(PAccount account) = 0;

    ///Subscribe market event stream
    template<StreamType<MarketInstrumentStreamTypeItem> T>
    EventStream<T> subscribe() {
        auto x =  subscribe_stream_internal(T::type, stream_params<T>);
        if (x) return EventStream<T>(std::move(x));
        else return EventStream<T>();
    }

};




struct Quote : MarketInstrumentStreamTypeItem {
    Decimal bid;
    Decimal bid_size;
    Decimal ask;
    Decimal ask_size;
    std::chrono::system_clock::time_point time;
    static constexpr Type type = "quote";
    Quote &view() {return *this;}
};

struct Trade : MarketInstrumentStreamTypeItem {
    Decimal price;
    Decimal size;
    std::chrono::system_clock::time_point time;
    Trade &view() {return *this;}
    static constexpr Type type = "trade";
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
};

template<unsigned int _interval_sec>
struct ClosedBarInterval: ClosedBar, MarketInstrumentStreamTypeItem{
    constexpr static auto params =ParamType {{},_interval_sec};
    std::chrono::system_clock::time_point interval_begin() const {
        return std::chrono::system_clock::time_point(std::chrono::seconds(interval_index * _interval_sec));
    }
    std::chrono::system_clock::time_point interval_end() const {
        return std::chrono::system_clock::time_point(std::chrono::seconds(interval_index * (_interval_sec+1)));
    }
};

static_assert(HasStreamParams<ClosedBarInterval<300> >);

struct OrderBookView {
    std::span<OrderBookLevel> bids = {};
    std::span<OrderBookLevel> asks = {};
     std::chrono::system_clock::time_point *time = nullptr;

    OrderBookView() = default;
    OrderBookView(std::span<OrderBookLevel> bids,std::span<OrderBookLevel> asks,std::chrono::system_clock::time_point *time)
        :bids(bids),asks(asks),time(time) {}

    OrderBookView &operator=(const OrderBookView &other) noexcept{
        if (this != &other) {
            auto dbids = std::min(bids.size(), other.bids.size());
            auto dasks = std::min(asks.size(), other.asks.size());
            std::copy_n(other.bids.begin(), dbids, bids.begin());
            std::copy_n(other.asks.begin(), dasks, asks.begin());
            if (time && other.time) *time = *other.time;
        }        
        return *this;
    }
};


template<unsigned int depth>
struct OrderBook: MarketInstrumentStreamTypeItem {
public:
    std::chrono::system_clock::time_point time;
    std::array<OrderBookLevel,depth> bids; 
    std::array<OrderBookLevel,depth> asks;     

    OrderBookView view() {return OrderBookView(bids,asks,&time);}

    static constexpr Type type = "orderbook_snapshot";
};

struct TradeCounter : public MarketInstrumentStreamTypeItem {
    static constexpr Type type = "trade_counters";
    /// total count of trades
    std::uint64_t trades = 0;
    /// total volume - it can reset when stream is reopened
    Decimal volume ={};
    /// last price
    Decimal last_price = {};

    std::chrono::system_clock::time_point time;
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