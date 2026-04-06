#pragma once

#include "defs.hpp"
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

    virtual Info get_info() const = 0;

    ///Internal
    virtual std::shared_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams &params) = 0;
    
    ///Create tradable instrument from the instrument
    /**
      @param account associated account
      @return reference to tradable instrument, can be nullptr if not available for trading with this account
     */
    virtual awaitable<PTradableInstrument> create_tradable_instrument(PAccount account) = 0;

    ///Subscribe market event stream
    /**
    @tparam T type of item determines type of stream. 
    @return shared pointer to IMarketEventStream handling stream of given type. Can't return nullptr, but for unsupported
    streams, it can return dummy stream which throws exception on access    

    @note The stream is returned already subscribed. You can start reading from it immediately. To unsubscribe, 
    just destroy the pointer or call close() on the stream.
     */
    template<StreamType T>
    EventStream<T> subscribe() const {
        auto x =  subscribe_stream_internal(T::type, stream_params<T>);
        if (x) return EventStream<T>(std::static_pointer_cast<typename EventStream<T>::ViewType>(x));
        else return EventStream<T>();
    }

};





}