#pragma once

#include "coro_support.h"
#include "fill.h"
#include "order.h"
#include "timer.h"
#include "config.h"
#include "log.h"
#include "market_event.h"
#include "mq.h"
#include "serialize.h"
#include <span>
#include "service.h"
#include "variables.h"

namespace trading_api {





class IContext : public IService{
public:

    virtual ~IContext() = default;

    ///request update account
    virtual void update_account(const Account &a) = 0;

    ///request update instrument
    virtual void update_instrument(const Instrument &i) = 0;

    virtual std::span<const Account> get_accounts() const = 0;

    virtual std::span<const Instrument> get_instruments() const = 0;

    virtual const Config &get_config() const = 0;

    ///Retrieves current time
    virtual Timestamp get_event_time() const = 0;

    ///Sets time, which calls function wrapped into runnable object. It is still bound to strategy
    virtual void set_timer(Timestamp at, TimerEventCB fnptr = {}, TimerID id = 0) = 0;

    ///Cancel timer
    virtual bool clear_timer(TimerID id) = 0;

    ///Place an order
    virtual Order place(const Instrument &instrument, const Account &account, const Order::Setup &setup, std::string_view label) = 0;

    ///Creates and bind an order to an instrument
    virtual Order bind_order(const Instrument &instrument, const Account &account, std::string_view label) = 0;

    ///Cancel given order
    virtual void cancel(const Order &order) = 0;

    ///Replace order
    virtual Order replace(const Order &order, const Order::Setup &setup, std::string_view label) = 0;

    ///Retrieve recent fills
    virtual Fills get_fills(std::size_t limit, std::string_view filter = {}) const = 0;

    ///Retrieve recent fills
    virtual Fills get_fills(Timestamp tp, std::string_view filter = {}) const = 0;

    virtual Positions load_positions(std::string_view filter = {}) const = 0;
    virtual Trades load_closed(Timestamp limit, std::string_view filter = {}) const  = 0;

    ///set persistent variable
    /**
     * It is expected, that key-value database is involved.
     * @param var_name variable name - any binary content is allowed
     * @param value value as string - any binary content is allowed
     */
    virtual void set_var(std::string_view var_name, std::string_view value) = 0;

    virtual std::string get_var(std::string_view var_name) const = 0;

    virtual VarSet<std::string_view> get_vars(std::string_view prefix) const = 0;

    virtual VarSet<std::string_view> get_vars(std::string_view start, std::string_view end) const = 0;



    ///Deletes persistently stored variable
    /**
     * @param var_name name of variable.
     *
     * @note Deleted variable releases some space in database and it
     * also stops appearing in list of variables supplied during init()
     */
    virtual void unset_var(std::string_view var_name) = 0;

    ///allocate equity on given account
    virtual void allocate(const Account &a, double equity) = 0;

    ///subscribe market events
    virtual void subscribe(MarketEventType type, const Instrument &i) = 0;

    ///unsubscribe instrument
    virtual void unsubscribe(MarketEventType type, const Instrument &i) = 0;

    virtual void mq_subscribe_channel(std::string_view channel) = 0;
    virtual void mq_unsubscribe_channel(std::string_view channel) = 0;
    virtual void mq_send_message(std::string_view channel, std::string_view msg) = 0;


    ///retrieve one shot market event
    virtual void update_market(const Instrument &i, MarketEventType type) = 0;


    virtual Log get_logger() const = 0;

};

}

