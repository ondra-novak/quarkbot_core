#pragma once

#include "ifc/defs.hpp"
#include "impl/streaming/lock_free_publisher.hpp"
#include "ifc/market_instrument.hpp"
#include <memory>
namespace quarkbot {
namespace bitfinex {


class BFXInstrument: public IMarketInstrument {
public:
    
    BFXInstrument(const Info info, PExchange owner)
        :_cur_info(info)
        ,_owner(std::move(owner)) {}

    virtual PExchange get_exchange() const override {
        return _owner;
    }
    virtual const Info &get_info() const override  {
        return _cur_info;
    }

    virtual std::unique_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams *params) override;

    virtual awaitable<PTradableInstrument> create_tradable_instrument(PAccount account) override;


    void info_updated(const InstrumentInfo &info);

protected:

    using InstrumentInfoPublisher = LockFreePublisher<InstrumentInfo, 1>;

    std::mutex _mx;
    Info _cur_info;
    PExchange _owner;
    std::weak_ptr<InstrumentInfoPublisher> _info_publisher;


    std::shared_ptr<InstrumentInfoPublisher> get_or_create_info_publiser();



};


}
}