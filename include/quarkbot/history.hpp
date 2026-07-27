#pragma once

#include "abstract/ihistory.hpp"
#include "quarkbot/market_instrument.hpp"
#include "quarkbot/stream/history.hpp"
#include "utils/wrapper.hpp"
namespace quarkbot {


///History adapter allows to request historical data for the instrument
class HistoryAdapter: public Wrapper<IHistoryAdapter> {
public:
    using Wrapper<IHistoryAdapter>::Wrapper;


    ///Request historical data for the instrument
    /**
        @tparam T type of historical data event - must be a stream type
        @param req request parameters
        @return event stream which produces historical data events. The stream is closed when all data has
        been produced or when the request has been interrupted by the stop token.
    */
    template<MarketInstrumentStream T>
    EventStream<T> get_history(const HistoryDataRequest &req) const {
        auto x = _ptr->subscribe_stream(class_hash<typename StreamViewType<T>::type>, &req);
        if (x) return EventStream<T>::from_base(std::move(x));
        return {};
    }

}

}