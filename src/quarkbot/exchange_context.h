#pragma once

#include "awaiter.h"
#include "config.h"
#include "order.h"
#include "fill.h"
#include "network.h"
#include "market_event.h"

#include "log.h"
namespace quarkbot {

class IEvantTarget;

///Counterpart object to IExchangeService - to communicate from exchange to core
class IExchangeContext {
public:


    virtual ~IExchangeContext() = default;

    virtual bool income_data(const MarketEvent &event) = 0;
    ///call this function when account is updated
    virtual void object_updated(const Account &i, AsyncResult<void> st) = 0;
    ///call this function when instrument is updated
    virtual void object_updated(const Instrument &i, AsyncResult<void> st) = 0;
    virtual void object_updated(const Instrument &i, MarketEventType type, AsyncResult<MarketEventData> ev) = 0;
    ///call this function when order's state changed
    /**
     * As the orders are const, you cannot change state of the order directly. The
     * state is changed during processing the event because orders are in possesion
     * of the strategy
     *
     * @param order order instance
     * @param state new state
     * @param reason optional reason (for the state)
     * @param message optional string message if error
     */
    virtual void order_report(const Order &order, Order::Report report) = 0;

    virtual const Config &get_config() const = 0;

    ///Convert this object to exchange
    virtual ExchangeInfo get_exchange_info() const = 0;

    virtual Log get_log() const = 0;

    ///Create Network object
    virtual Network get_network() const = 0;

    using TimerCallback = Function<void(Timestamp)>;

    virtual void set_timer(Timestamp at, TimerCallback fnptr, TimerID id = 0) = 0;

    virtual bool clear_timer(TimerID id) = 0;

    virtual void on_stop_requested(Function<void()> &&cb) = 0;

    virtual void stop() = 0;


};

}


