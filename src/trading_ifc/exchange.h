#pragma once
#include "iexchange.h"
namespace trading_api {


class Exchange : public IExchange {
public:

    Exchange() = default;
    Exchange(const Exchange &) = delete;
    Exchange &operator=(const Exchange &) = delete;


    virtual void on_start() {}

    ///call this function when market event for given instrument arrived (subscription)
    void send_market_event(const Instrument &i, const MarketEvent &t) const {
        _ctx->income_data(i, t);
    }
    ///call this function when account is updated
    void object_updated(const Account &a, AsyncResult<void> st) const {
        _ctx->object_updated(a, std::move(st));
    }
    ///call this function when instrument is updated
    void object_updated(const Instrument &i, AsyncResult<void> st) const {
        _ctx->object_updated(i, std::move(st));
    }
    void object_updated(const Instrument &i, MarketEventType type, AsyncResult<MarketEvent> ev) const {
        _ctx->object_updated(i, std::move(type), std::move(ev));
    }
    void order_report(const Order &order, Order::Report report, Fills fills) const {
        _ctx->order_report(order, std::move(report), std::move(fills));
    }

    ///allows to convert this to ExchangeInfo (required by instruments and accounts)
    operator ExchangeInfo() const {
        return _ctx->get_exchange_info();
    }

    ///convert to exchange
    ExchangeInfo get_exchange_info() const {
        return _ctx->get_exchange_info();
    }

    Log get_log() const {
        return _ctx->get_log();
    }

    Network get_network() const {
        return _ctx->get_network();
    }

    const Config &get_config() const {
        return _ctx->get_config();
    }



protected:
    IExchangeContext *_ctx = {};

    virtual void init(IExchangeContext *ctx) {
        _ctx = ctx;
        on_start();
    }
};


///Export the strategy, so the strategy can be loaded by the loader
/**
 * @param class_name name of the strategy (class name). The strategy is
 * registered under its name
 */
#define EXPORT_EXCHANGE(class_name) ::trading_api::IModule::Factory<IExchange> exchange_reg_##class_name(#class_name, std::in_place_type<class_name>)

///Export the strategy, but specify other name
/**
 * @param class_name class name of strategy
 * @param export_name exported name
 */
#define EXPORT_EXCHANGE_AS(class_name, export_name) ::trading_api::IModule::Factory<IExchange> exchange_reg_##class_name(export_name, std::in_place_type<class_name>)



}
