#pragma once
#include "basic_order.h"
#include "iexchange.h"
namespace quarkbot {


class Exchange : public IExchange {
public:

    Exchange() = default;
    Exchange(const Exchange &) = delete;
    Exchange &operator=(const Exchange &) = delete;

    template<typename T>
    using Awaitable = AwaitableResult<T,  Function<void(Function<void(AsyncResult<T>)>)> >;


    virtual void on_start() {}

    ///call this function when market event for given instrument arrived (subscription)
    /**
     * @param i associated instrument
     * @param type type of market event/subscription
     * @param data market event data, you can use MarketEventFactory to fast allocate these objects
     * @retval true continue sending next data
     * @retval false passive unsubscribe (same as call unsubscribe)
     */
    bool send_market_event(const Instrument &i, MarketEventType type, const MarketEvent &data) const {
        return _ctx->income_data(i, type, data);
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

    ///Sets timer to call specific event
    /**
     * Schedules function call at given time point.
     * @param at time point, it doesn't need to be real time (especially in simulation)
     * @param cb callback function. It receives current time. This is important
     * argument in case, that environment is simulated (in which case, simulated
     * time is different than real time)
     * @param id id of timer (optional)
     */
    template<std::invocable<Timestamp> CB>
    void set_timer(Timestamp at, CB &&cb, TimerID id = 0) {
        _ctx->set_timer(std::move(at), std::forward<CB>(cb), std::move(id));
    }
    ///clear specified timer
    /**
     * @param id identifier of timer
     *
     */
    void clear_timer(TimerID id) {
        _ctx->clear_timer(id);
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
