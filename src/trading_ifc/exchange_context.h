#pragma once

#include "config.h"
#include "order.h"
#include "fill.h"
#include "async.h"
#include "network.h"

#include "log.h"
namespace trading_api {

class IEvantTarget;

///Counterpart object to IExchangeService - to communicate from exchange to core
class IExchangeContext {
public:


    virtual ~IExchangeContext() = default;

    virtual void income_data(const Instrument &i, const MarketEvent &t) = 0;
    ///call this function when account is updated
    virtual void object_updated(const Account &i, AsyncStatus st) = 0;
    ///call this function when instrument is updated
    virtual void object_updated(const Instrument &i, AsyncStatus st) = 0;
    virtual void object_updated(const Instrument &i, AsyncStatus st, MarketEvent ev) = 0;
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
    virtual void order_state_changed(const Order &order, const Order::Report &report) = 0;
    ///call this function for every fill on the order
    /**
     * @param order order instance
     * @param fill fill information
     */
    virtual void order_fill(const Order &order, const Fill &fill) = 0;
    ///call this function for every restored order from set passed to restore_orders()
    /**
     * @param context pointer which has been passed to restore_orders.
     * @param order restored order instance
     */
    virtual void order_restore(void *context, const Order &order) = 0;

    virtual const Config &get_config() const = 0;

    ///Convert this object to exchange
    virtual ExchangeInfo get_exchange_info() const = 0;

    virtual Log get_log() const = 0;

    ///Create Network object
    virtual Network get_network() const = 0;


};

}


