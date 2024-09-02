#pragma once
#include "basic_order.h"
#include "iexchange.h"
namespace quarkbot {


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
    void order_report(const Order &order, Order::Report report) const {
        _ctx->order_report(order, std::move(report));
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

    ///Default implementation for order managment, when BasicOrder is used
    virtual Order create_order(const Instrument &instrument, const Account &account, const Order::Setup &setup, std::string_view label) override {
        return Order(std::make_unique<BasicOrder>(instrument, account, setup, label, Order::Origin::strategy));
    }
    ///Default implementation for order managment, when BasicOrder is used
    virtual Order create_order_replace(const Order &replace, const Order::Setup &setup, std::string_view label) override {
        return Order(std::make_unique<BasicOrder>(replace, setup, label, Order::Origin::strategy));
    }
    ///Default implementation for order managment, when BasicOrder is used
    virtual void order_apply_report(const Order &order, const Order::Report &report) override {
        BasicOrder::apply_report(order, report);
    }
    Order restore_basic_order(const Instrument &instrument, const Account &account, const Order::Setup &setup, std::string_view label) {
        std::shared_ptr<BasicOrder> x = std::make_unique<BasicOrder>(instrument, account, setup, label, Order::Origin::restored);
        x->get_status().state = Order::State::restoring;
        return Order(x);
    }



protected:
    IExchangeContext *_ctx = {};

    virtual void init(IExchangeContext *ctx) override {
        _ctx = ctx;
        on_start();
    }
};


///Export the strategy, so the strategy can be loaded by the loader
/**
 * @param class_name name of the strategy (class name). The strategy is
 * registered under its name
 */
#define EXPORT_EXCHANGE(class_name) ::quarkbot::IModule::Factory<IExchange> exchange_reg_##class_name(#class_name, std::in_place_type<class_name>)

///Export the strategy, but specify other name
/**
 * @param class_name class name of strategy
 * @param export_name exported name
 */
#define EXPORT_EXCHANGE_AS(class_name, export_name) ::quarkbot::IModule::Factory<IExchange> exchange_reg_##class_name(export_name, std::in_place_type<class_name>)



}
