#pragma once

#include "abstract/imarket_instrument.hpp"
#include "abstract/ipublisher.hpp"
#include "instrument_description.hpp"
#include "stream/snapshot.hpp"
#include "utils/wrapper.hpp"
namespace quarkbot {

template<typename T>
concept MarketInstrumentStream = requires {
    typename T::MarketInstrumentStream;
};


class Exchange;
class Account;
class TradableInstrument;

///Market instrument represents financial instrument. It only provides information about the instrument and allows to subscribe to its streams.
/**
    You cannot use this to place orders, for that you need to create TradableInstrument by calling create_tradable_instrument() with account as parameter.
*/
class MarketInstrument: public Wrapper<IMarketInstrument> {
public:

    using Info = IMarketInstrument::Info;

    using Wrapper<IMarketInstrument>::Wrapper;

    ///Get exchange associated with this instrument
    Exchange get_exchange() const;
    ///Get information about this instrument
    const Info &get_info() const {return _ptr->get_info();}

    ///Create tradable instrument for this market instrument and given account.
    TradableInstrument create_tradable_instrument(const Account &account) const;


    template<MarketInstrumentStream T>
    requires(StreamWithoutParam<T> || StreamWithConstantParam<T>)
    EventStream<T> subscribe() const {
        return _ptr->subscribe<T>();
    }

    template<MarketInstrumentStream T>
    requires(StreamWithParam<T>)
    EventStream<T> subscribe(typename T::Param param) const {
        return _ptr->subscribe<T>(param);
    }

    
    ///Receive snapshot 
    /**
        @param v variable which receives snapshot
        @param token stop token allows to interrupt asynchronous operation prematurelly - you can use this to implement timeout
        @retval true awaitable: value has been received
        @retval false awaitable: operation has been interrupted

        @note The implementation depends on the adapter. A simple version subscribes to the Trade and Quote streams 
              and gets the first event of each stream which it then returns as a result. Another implementation 
              can use a REST API request to directly request a snapshot from the exchange. Such a request 
              can be included in rate limiting rules. Therefore, you should avoid calling this function frequently.
              If you need snapshots more frequently (less than 1 minute), it is much easier to subscribe to a stream        
    */
    awaitable<bool> receive_snapshot(Snapshot &v, std::stop_token token = {}) {
        return _ptr->receive_snapshot(v, std::move(token));
    }
    
};


///Stream is updated when some informations about instrument changed
struct InstrumentInfo :  InstrumentGeometry  {
    struct MarketInstrumentStream {};

};

}