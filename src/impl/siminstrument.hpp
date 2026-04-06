#pragma once

#include "ifc/market_instrument.hpp"
#include "simexchange.hpp"
#include <memory>

namespace quarkbot {



class SimInstrument final: public IMarketInstrument, public std::enable_shared_from_this<SimInstrument> {
public:


    SimInstrument(Info config, std::shared_ptr<SimExchange> exchange)
        :_exchange(std::move(exchange)),_cfg(config) {}

    virtual PExchange get_exchange() const override {return _exchange;}
    virtual Info get_info() const override {return _cfg;}
    virtual std::shared_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams &params)  override {
        return _exchange->subscribe_stream(shared_from_this(), nullptr, type, params);
    }
    virtual awaitable<PTradableInstrument> create_tradable_instrument(PAccount account) override {
        auto acc = std::dynamic_pointer_cast<SimAccount>(account);
        if (!acc) return std::  nullopt; //return nullopt because it throws an exception if accessed.
        return _exchange->create_tradable_instrument(shared_from_this(), acc);
    }


    auto get_sim_exchange() const {return _exchange;}
protected:
    std::shared_ptr<SimExchange> _exchange;
    Info _cfg;

};

}