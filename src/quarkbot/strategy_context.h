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
#include "service.h"
#include "variables.h"

#include <span>
#include <deque>

namespace quarkbot {





class IContext : public IService{
public:



    virtual ~IContext() = default;

    ///request update account
    virtual void update_account(const Account &a, Function<void(AsyncResult<void>)> &&cb) = 0;

    ///request update instrument
    virtual void update_instrument(const Instrument &i, Function<void(AsyncResult<void>)> &&cb) = 0;

    ///retrieve one shot market event
    virtual void update_market(const Instrument &i, MarketEventType type, Function<void(AsyncResult<MarketEventData>)> &&cb) = 0;

    ///await for order report and calls function when report is ready
    virtual void receive_order_report(const Order &order, Function<void(AsyncResult<std::span<Fill>  >)> &&cb) = 0;

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
    virtual void mq_send_message(std::string_view channel, std::string_view msg, IMQBroker::ConversationID cid) = 0;




    virtual Log get_logger() const = 0;

    virtual std::string_view get_strategy_name() const = 0;

    /// Append a point to a series
    /**
     * @param series_name Name of the series
     * @param point_data Binary representation of a point (serialized to binary)
     * @return Index of the newly created point
     */
    virtual std::uint64_t series_add_point(std::string_view series_name, std::string_view point_data) = 0;

    /// Erase older points from a series
    /**
     * @param series_name Name of the series
     * @param index_and_less Highest index of points to erase
     */
    virtual void series_erase_points(std::string_view series_name, std::uint64_t index_and_less) = 0;

    ///Loads points of series
    /**
     * @param name
     * @return instance of Values - iteratable container of values in
     * binary serialized format for given series
     */
    virtual ValueStream<std::string_view> load_series(std::string_view name) const = 0;


    virtual void on_idle(Function<void(AsyncResult<void> )> &&fn) = 0;


    virtual void on_market_event(Function<void(AsyncResult<MarketEvent>)> &&callback) = 0;

    virtual void on_orders_restored(Function<void(AsyncResult<std::span<Order> >)> &&callback) = 0;

    virtual void on_mq_message(Function<void(AsyncResult<IMQBroker::Message>)> &&callback) = 0;
    ///stops the strategy
    /**
     * Causes that strategy is stopped. No more events can be generated,
     * including timed events. All asynchronous calls are exited with
     * an canceled exception.
     *
     * @note this should be last code in the strategy. The function must
     * be called right before return from the event. Not garantee, that
     * any code below this call will execute.
     *
     * If your strategy needs to close orders or positions, it must handle this
     * before stop()
     */
    virtual void stop() = 0;

    ///Defines a function which is called when stop is requested
    /**
     * If this function is not defined, the strategy is stopped immediately
     * on the stop request. You can handle this situation by suppling
     * own function. This function should eventually call stop()
     *
     * @param fn function which handles stop request. Note there can be
     * only one handler.
     */
    virtual void on_stop_requested(Function<void(AsyncResult<void>)> &&fn) = 0;
    ///returns true, if stop has been requested
    /**
     * @retval false normal operation
     * @retval true stop has been requested
     */
    virtual bool is_stop_requested() const = 0;
};



}

