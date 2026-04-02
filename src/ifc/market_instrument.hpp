#pragma once

#include "defs.hpp"
#include "types.hpp"
#include "ifc/streaming.hpp"
#include <memory>
namespace quarkbot {




class IMarketInstrument {
public:


    struct Info : ContractInfo {
        Decimal min_lot_size = {};
        Decimal lot_size_increment = {};
        Decimal price_increment = {};
        double min_volume = 0.0;
        double fee_rate_maker = 0.0;
        double fee_rate_taker = 0.0;
    };


    virtual ~IMarketInstrument() = default;
    ///Retrieves quote currency (price)
    virtual PUnderlyingCurrency get_quote_currency() const = 0;
    ///Returns the asset currency of the market instrument, This can be NULL for contracts that do not have an asset currency
    virtual PUnderlyingCurrency get_asset() const = 0;
    ///Retrieves currency of PnL. It can be different for inverse contracts. For normal contracts, it returns get_quote_currency
    virtual PUnderlyingCurrency get_pnl_currency() const = 0;

    virtual PExchange get_exchange() const = 0;

    virtual Info get_info() const = 0;


    ///Internal
    virtual std::shared_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams &params) const = 0;
    
    ///Create tradable instrument from the instrument
    /**
      @param account associated account
      @return reference to tradable instrument, can be nullptr if not available for trading with this account
     */
    virtual awaitable<PTradableInstrument> create_tradable_instrument(PAccount account) const = 0;


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

    virtual std::string_view get_name() const = 0;
};





}