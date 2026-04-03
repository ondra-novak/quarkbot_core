#pragma once

#include "ifc/market_instrument.hpp"
#include "impl/simexchange.hpp"

namespace quarkbot {

class SimInstrument: public IMarketInstrument {
public:


    SimInstrument(Info config, std::shared_ptr<SimExchange> exchange, std::string name)
        :_exchange(std::move(exchange)),_cfg(config),_name(std::move(name)) {}

    virtual PExchange get_exchange() const override {return _exchange;}
    virtual Info get_info() const override {return _cfg;}
    virtual std::shared_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams &params) const override {
        return _exchange->subscribe_stream(this, nullptr, type, params);
    }
    virtual awaitable<PTradableInstrument> create_tradable_instrument(PAccount account) const override {
        return _exchange->create_tradable_instrument(this, account.get());
    }
    virtual std::string_view get_name() const override {
        return _name;
    }

protected:
    std::shared_ptr<SimExchange> _exchange;
    Info _cfg;
    std::string _name;

};

}