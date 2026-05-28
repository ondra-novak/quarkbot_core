#pragma once

#include "abstract/imarket_instrument.hpp"
namespace quarkbot {


class Account;
class TradableInstrument;

///Market instrument represents financial instrument. It only provides information about the instrument and allows to subscribe to its streams.
/**
    You cannot use this to place orders, for that you need to create TradableInstrument by calling create_tradable_instrument() with account as parameter.
*/
class MarketInstrument {
public:

    using Info = IMarketInstrument::Info;

    ///Get exchange associated with this instrument
    PExchange get_exchange() const {return _state->get_exchange();}
    ///Get information about this instrument
    const Info &get_info() const {return _state->get_info();}

    ///Create tradable instrument for this market instrument and given account.
    TradableInstrument create_tradable_instrument(const Account &account) const;

    template<StreamType<MarketInstrumentStreamTypeItem> T>
    EventStream<T> subscribe() {
        auto x =  _state->subscribe_stream_internal(T::type, stream_params<T>);
        if (x) return EventStream<T>::from_base(std::move(x));
        else return EventStream<T>::create_null();
    }

    MarketInstrument(std::shared_ptr<IMarketInstrument> state):_state(std::move(state)){}
    
    bool operator==(const MarketInstrument &) const = default;

    auto get_handle() const {return _state;}

protected:
    std::shared_ptr<IMarketInstrument> _state;
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