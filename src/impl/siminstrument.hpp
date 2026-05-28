#pragma once

#include "ifc/market_instrument.hpp"
#include "ifc/types.hpp"
#include "simexchange.hpp"
#include <memory>
#include <stdexcept>

namespace quarkbot {



class SimInstrument final: public IMarketInstrument, public std::enable_shared_from_this<SimInstrument> {
public:

    SimInstrument(const Info &config, std::shared_ptr<SimExchange> exchange)
        :_exchange(std::move(exchange)),_cfg(config) {}

    virtual PExchange get_exchange() const override {return _exchange;}
    virtual const Info &get_info() const override {return _cfg;}
    virtual std::unique_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams *params)  override {
        return _exchange->subscribe_stream(shared_from_this(), nullptr, type, params);
    }
    virtual PTradableInstrument create_tradable_instrument(PAccount account) override {
        auto acc = std::dynamic_pointer_cast<SimAccount>(account);
        if (!acc) throw std::runtime_error("Incompatible account (create_tradable_instrument)");
        return _exchange->create_tradable_instrument(shared_from_this(), acc);
    }


    auto get_sim_exchange() const {return _exchange;}
protected:
    std::shared_ptr<SimExchange> _exchange;
    const Info &_cfg; //held on exchange

};

}