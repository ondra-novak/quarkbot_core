#pragma once
#include "istrategy.h"
#include "awaiter.h"
#include "coro_support.h"
#include "signal.h"

#include <queue>

namespace quarkbot {

///Strategy base object - extend this object to create own strategy
class Strategy: public IStrategy {
public:

    using Decimal = ::quarkbot::Decimal;
    using Message = IContext::Message;


    ///Awaitable object
    /**
     * Awaitable object is placeholder for asynchronous result. It allows two operations
     * to retrieve the result
     *
     * 1. you can use co_await on this object, which pefroms suspenssion of the execution
     * and resumption when the result is awailable. The return value of co_await is
     * result of asynchronous operation. This requires to run code as Coroutine
     *
     * 2. you can attach a callback by operation `>> []{lambda}`. The result of the
     * operation is passed as AsyncResult object and you must use get() to retrieve
     * the result.
     *
     * @tparam T type of result. This can be void, so asynchronous operation has no
     * result, but you still receive information about completion. Result to the
     * callback function is carried as AsyncResult even if T is void (AsyncResult<void>).
     * In this case, you should check for exception, which can indiciate that operation
     * failed. This is not necessery in Coroutine, because co_await can throw that
     * exception as result.
     */
    template<typename T>
    using Awaitable = AwaitableResult<T,  Function<void(Function<void(AsyncResult<T>)>)> >;

    ///Specifies type of callback for given Awaitable
    template<typename T>
    using Callback = typename Awaitable<T>::Callback;

    ///Declaration of list of callbacks
    template<typename T>
    using CallbackList = std::deque<Callback<T> >;

    ///strategy has always default constructor
    Strategy() = default;
    ///stategy cannot be copied
    Strategy(const Strategy &) = delete;
    ///stategy cannot be copied
    Strategy &operator=(const Strategy &) = delete;

protected: //recommended overrides

    ///you need to redefine this function if you need to generate configuration schema
    /**
     * This function can be called before context is initialized. If it is called,
     * it is probably only for configuration schema and strategy will not be probably
     * started in this time.
     *
     * @return configuration schema.
     */
    MT_SAFE virtual ConfigSchema get_config_schema() const override {
        return {};
    }

    ///this is strategy's entry point. You need to declare own implementation
    /**
     * Function is called as an event, but only once at the beginning. In this
     * case, everything is correctly initialized and strategy can do anything it
     * wants.
     *
     * If there are stored opened orders, they report arrives immediately after
     * strategy finishes on_start(). If you need a special processing after these
     * reports arrive, you can use on_idle() to schedule execution
     *
     * @note main() is coroutine. If your code is not want to be a coroutine, just
     * return value of base implementation
     */
    MT_UNSAFE virtual coro main() {
        return {};
    }

    ///Called when an uncaught exception is detected
    /**You can process the exception in this function.
     * The active exception is available via std::current_exception,
     * or via the throw-catch sequence
     *
     * If the function completes normally, the exception
     * is considered processed and the processing code proceeds normally.
     * However, if the function throws an exception, the exception is reported
     * to the log file and all active transactions are rollbacked
     */
    MT_UNSAFE virtual void on_unhandled_exception() override {
        throw;
    }



public:  //context API


    ///Register startup function
    /**
     * @code
     * co_await started()
     * @endcode
     *
     * Ensures that strategy started. This is required for coroutines
     * initiated before main() is called. If function is called after main(),
     * it has no suspend effect
     *
     * This is one of functions available before the context is initialized.
     *
     * Alternative usage:
     *
     * @code
     * started() >> [this]{
     *      //code called at start
     * }
     * @endcode
     *
     * @note NOT MT SAFE
     */
    MT_UNSAFE Awaitable<void> on_started() {
        if (_started) {
            return [=](auto &&fn){fn(AsyncResult<void>());};
        } else {
             return [this](auto &&fn){
                 _start_cbs.register_callback(std::move(fn));
             };
        }
    }


    ///Retrieve strategy name
    /** Strategy name is defined in configuration file. There can be
     * multiple instances of same strategy class. Each strategy instance
     * has an unique name. This function returns that name
     *
     * @return strategy name as defined in configuration
     */
    MT_SAFE std::string_view get_strategy_name() const {
        return _ctx->get_strategy_name();
    }

    ///Retrieve available accounts
    /**
     * @return available (configired) accounts
     */
    MT_SAFE std::span<const Account> get_accounts() const {return _ctx->get_accounts();}

    ///Retrieve available instruments
    /**
     * @return configired instruments
     */
    MT_SAFE std::span<const Instrument> get_instruments() const {return _ctx->get_instruments();}

    ///Retrieve strategy's configuration
    /**
     * @return configuration object
     */
    MT_SAFE const Config &get_config() const {return _ctx->get_config();}

    ///store value under a variable name in a persistent storage
    /**
     * Key-Value database is expected.
     *
     * @param key a string key under which the value will be stored
     * @param value a string value to be stored
     *
     * @note There is no limitation for key and value. You can use binary
     * content for both key and value. There is also no limit of size. However
     * keep in mind, that to store longer keys and values can have impact on
     * performance.
     *
     * @note store operation is asynchronous. Calling get() immediately
     * after set() can still return previous value
     */
    MT_UNSAFE void set_var(std::string_view key, std::string_view value) const {
        _ctx->set_var(key, value);
    }

    ///store value under a variable name in a persistent storage
    /**
     * Key-Value database is expected.
     *
     * @param key a string key under which the value will be stored
     * @param value any binary serializable value, which includes all basic types, enums, and classes
     * with trivial copy constructors (must not contain pointer)
     *
     * @note store operation is asynchronous.Calling get() immediately
     * after set() can still return previous value
     */
    template<SerializableType T>
    MT_UNSAFE void set_var(std::string_view key, const T &value) const {
        std::string s;
        Serializer::to_binary(std::back_inserter(s), value);
        _ctx->set_var(key,s);
    }

    ///unset variable
    /**
     * @param varname variale to unset
     * @note it is better to use unset_var instead of setting variable to empty string.
     */
    MT_UNSAFE void unset_var(std::string_view varname) const {
        _ctx->unset_var(varname);
    }

    ///retrieve stored value from database
    /**
     * @param varname name of variable
     * @return content. If the variable doesn't exist, returns empty string
     */
    MT_UNSAFE std::string get_var(std::string_view varname) const{
        return _ctx->get_var(varname);
    }

    ///retrieve stored value
    /**
     * @param key variable name
     * @param default_value default value in case that variable is undefined
     * @return content of variable or default value if doesn't exists
     */
    template<SerializableType T>
    MT_UNSAFE T get_var(std::string_view varname, const T &default_value) const{
        static_assert(!std::is_same_v<T, std::string_view>, "Can't return reference, use std::string");
        std::string s = _ctx->get_var(varname);
        if (s.empty()) return default_value;
        return Serializer::from_binary<T>(s.begin(), s.end());
    }

    ///retrieve stored value as optional (with detection whether is defined)
    /**
     * @param key variable name
     * @return content of variable as optional. If variable is not defined
     * return empty value
     */
    template<SerializableType T>
    MT_UNSAFE std::optional<T> get_var_opt(std::string_view varname) const{
        static_assert(!std::is_same_v<T, std::string_view>, "Can't return reference, use std::string");
        std::string s = _ctx->get_var(varname);
        if (s.empty()) return {};
        return Serializer::from_binary<T>(s.begin(), s.end());
    }

    ///retrieve multiple variables
    /**
     * @tparam T type of values
     * @param prefix defines common prefix
     * @return iteratable set of variables
     *
     * you can use this function to retrieve various tables. There is no limit, how
     * big tables can be retrieved as the database is iterated during processing results
     */
    template<typename T>
    MT_UNSAFE VarSet<T> get_vars(std::string_view prefix) const {
        return _ctx->get_vars(prefix);
    }

    ///retrieve multiple variables
    /**
     * @tparam T type of values
     * @param from key (inclusive)
     * @return to_key (inclusive)
     *
     * you can use this function to retrieve various tables. There is no limit, how
     * big tables can be retrieved as the database is iterated during processing results
     */
    template<typename T>
    MT_UNSAFE VarSet<T> get_vars(std::string_view from_key, std::string_view to_key) const {
        return _ctx->get_vars(from_key, to_key);
    }

    ///unset multiple variables
    /**
     * @param vars table of variables
     * @note you can delete whole table
     */
    template<typename T>
    MT_UNSAFE void unset_vars(const VarSet<T> &vars) const {
        auto h = vars.get_handle();
        bool s = h->init();
        while (s) {
            auto f = h->get();
            _ctx->unset_var(f.first);
            s = h->next();
        }
    }

    ///request update account
    /**
     * @param a account to update
     * @param fn function called when update is complete.
     *
     * @note In most cases, you don't need to update account when fill.
     */
    MT_UNSAFE Awaitable<void> update_account(const Account &a)  {
        return [&a,this](auto &&promise) {
            _ctx->update_account(a, std::move(promise));
        };
    }


    ///request update instrument
    /**
     * @param i instrument to update. The function refresh instrument features
     *  from the exchange
     * @param fn function which is called when update is complete
     *
     * @note you don't need to update instrument often.
     */

    MT_UNSAFE Awaitable<void> update_instrument(const Instrument &i)  {
        return [&i,this](auto &&promise) {
            _ctx->update_instrument(i, std::move(promise));
        };
    }


    ///Retrieves time of current event
    /**
     * @return time of current event (time when this event started). Note that
     * the function returns same value during processing the event, so don't
     * use it to measure time of calculations
     */
    MT_UNSAFE Timestamp get_event_time() const {return _ctx->get_event_time();}

    ///Sleep until specified time is reached
    /**
     * Sleep is asynchronous operation and can be used in two ways. The first
     * way uses a callback function, which is called at given time. The second
     * way is use co_await in a coroutine
     *
     * @param at specifies time point when execution continues
     * @param id specified identifier. It can be used to identify sleep operation to
     * cancel it by interrupt_sleep()
     *
     * @return a timer awaiter
     *
     * @code
     * sleep_until(at, id) >> [this]{
     *          //code continues at given time
     * };
     * @endcode
     *
     * @code
     * co_await sleep_until(at, id);
     * @endcode
     */
    MT_SAFE Awaitable<void> wait_until(Timestamp at, TimerID id = 0) {
        return [=,this](auto &&cb) {
            _ctx->set_timer(at, [cb = std::move(cb)]{
                cb(AsyncResult<void>(std::in_place));
            },id);
        };
    }

    ///Sleep for specified duration
    /**
     * Sleep is asynchronous operation and can be used in two ways. The first
     * way uses a callback function, which is called at given time. The second
     * way is use co_await in a coroutine
     *
     * @param dur specifies duration
     * @param id specified identifier. It can be used to identify sleep operation to
     * cancel it by interrupt_sleep()
     *
     * @return a timer awaiter
     *
     * @code
     * sleep_for(std::chrono::seconds(2)) >> [this]{
     *          //code continues at given time
     * };
     * @endcode
     *
     * @code
     * co_await sleep_for(std::chrono::seconds(2));
     * @endcode
     */
    template<typename A, typename B>
    MT_UNSAFE Awaitable<void> wait_for(std::chrono::duration<A,B> dur, TimerID id = 0) {
        return wait_until(get_event_time()+dur, id);
    }

    ///Interrupts a sleep operation
    /**
     * @param id specified identifier of sleep operation to interrupt.
     * @retval true sleep interrupted
     * @retval false identifier not found
     *
     * @note once the sleep is interrupted, the callback function cannot be called. If
     * sleep operation is co_awaited, then an AsyncCallException is thrown inside in
     * coroutine (coroutine is finished during this call)
     */
    MT_UNSAFE bool interrupt_wait(TimerID id) {return _ctx->clear_timer(id);}
    ///Place an order
    /**
     * @param account account used to trade the instrument
     * @param instrument instrument on where order will be placed
     * @param setup order type and configuration
     * @param label custom label (don't need to be unique). The label can be retrieves by get_label()
     * @return created order
     */
    MT_UNSAFE Order place_order(const Account &account, const Instrument &instrument, const Order::Setup &setup, std::string_view label = {}) {
        return _ctx->place(instrument, account, setup, label);
    }

    ///Creates an order, which is asociated with an instrument, but it is not placed
    /**
     * You can use replace() function to place the order with new setup. This
     * allows to track single order without need to know, whether order is actually
     * placed or not
     *
     * @param account associated account
     * @param instrument associated instrument
     * @param label custom label.
     *
     * @return dummy order (can be replaced)
     */
    MT_UNSAFE Order bind_order(const Account &account, const Instrument &instrument, std::string_view label = {}) const {
        return _ctx->bind_order(instrument, account, label);
    }

    ///Cancel given order
    /**
     * @param order order to cancel
     * @note cancel is an asynchronous operation. You will receive order status update
     * once the order is canceled
     * @note you cannot cancel associated or discarded order. You won't receive status
     * in this case
     */
    MT_UNSAFE void cancel_order(const Order &order) const {_ctx->cancel(order);}

    ///Replace order
    /**
     * @param order order to replace.
     * @param setup new setup of the order
     * @param label custom label. If this value is empty, the function uses label of the
     * order being replaced.
     *
     * @return new order
     */
    MT_UNSAFE Order replace_order(const Order &order, const Order::Setup &setup, std::string_view label = {})  {
        return _ctx->replace(order, setup, label.empty()?order.get_label():label);
    }

    ///Installs awaiter which is signaled when list of active orders is received
    /**
     * This function must be used at least in main() or prior. By using it
     * in any other event always returns an empty array
     *
     * @code
     * coro<void> main() {
     *     auto active = co_await active_orders();
     *     for (auto order: active) {
     *         process_order_coro(order); //run coroutine for every order
     *     }
     * }
     * @endcode
     *
     *
     * @return awaitable object which returns list of active orders. These
     * orders has been placed during previous run, they are stored in the
     * database in active state.
     *
     * This function only carries instances of these orders, but not
     * their recent report and fills. The recent report is retrieved
     * through on_report()
     */
    MT_UNSAFE Awaitable<std::span<Order> > on_restored_orders() {
        return [this](auto &&promise) {
            _ctx->on_orders_restored(std::move(promise));
        };
    }


    ///Request awaiter for order report
    /**
     * @param order order
     * @return awaiter. Awaiter can be used to co_await, or you can attach a callback.
     * Note this is one-shot action - only first report is received by callback, you
     * need to request report again. Result of co_await operation is fills. This
     * array can be empty, which means, that only status changed, without fills
     *
     * @note on_order_report() is always called.
     */
    MT_UNSAFE Awaitable<std::span<Fill> > on_report(const Order &order) {
        return [=,this](auto &&cb) {
            _ctx->receive_order_report(order, std::move(cb));
        };
    }


    ///Retrieve last fills
    /**
     * @param limit count of fills (to the history)
     * @param filter specifies filter. The filter is matched agains the label. If the
     * label starts with specified filter, the fill is included. If the filter
     * is empty, all fills are included
     * @return list of fills, ordered descending in time (most recent fill is the first)
     * @note if called from on_fill, the recent fill won't be in the database yet
     */
    MT_UNSAFE Fills get_fills(std::size_t limit, std::string_view filter = {}) const {
        return _ctx->get_fills(limit, filter);
    }

    ///Retrieve last fills
    /**
     * @param limit a timestamp in history
     * @param filter specifies filter. The filter is matched agains the label. If the
     * label starts with specified filter, the fill is included. If the filter
     * is empty, all fills are included
     * @return list of fills, ordered descending in time (most recent fill is the first)
     * @note if called from on_fill, the recent fill won't be in the database yet
     */
    MT_UNSAFE Fills get_fills(Timestamp tp, std::string_view filter = {}) const {
        return _ctx->get_fills(tp, filter);
    }

    ///Retrieve openeded positions from the database
    /**
     * @param filter specifies filter for positions.If the
     * label starts with specified filter, the position is included. If the filter
     * is empty, all positions are included
     *
     * @return list of opened positions.
     * @note returned list is aggregation of fills from the database, and don't need
     * to match opened positions reported by the exchange
     * @note if there are a lot of fills in the databse,
     *       this function can a take noticable time to process
     */
    MT_UNSAFE Positions get_opened_positions(std::string_view filter = {}) const {
        return _ctx->load_positions(filter);
    }
    ///Retrieve closed postions from the databse
    /**
     * @param limit time limit from which closed position is returned
     * @param filter specifies filter for positions.If the
     * label starts with specified filter, the position is included. If the filter
     * is empty, all positions are included
     * @return list of closed positions
     * @note returned list is aggregation of fills from the database, and don't need
     * to match opened positions reported by the exchange
     * @note if there are a lot of fills in the databse,
     *       this function can a take noticable time to process. The limit and filter
     *       won't to speed up the operation (both acts as HAVING key in SQL query)
     */
    MT_UNSAFE Trades get_closed_positions(Timestamp limit, std::string_view filter = {}) const {
        return _ctx->load_closed(limit, filter);
    }

    ///allocate equity for current strategy
    /**
     * If equity is shared between multiple strategies,
     * it helps to visualise, how much equity is unallocated or overallocated
     * @param a account
     * @param equity amount equity;
     *
     * @note not persistent. The strategy should update this value after any major
     * change in the strategy state, for example after fill.
     *
     */
    MT_UNSAFE void allocate_equity(const Account &a, double equity) {
         _ctx->allocate(a, equity);
    }

    ///Subscribe market data
    /**
     * @param type type of subscription (ticker, orderbook)
     * @param i instrument instance
     *
     * market events are passed through on_market_event. You can one-shot wait on
     * specific market event by function receive_event()
     */
    MT_UNSAFE void subscribe(MarketEventType type, const Instrument &i) {
        _ctx->subscribe(type, i);
    }

    ///Unsubscribe market data
    /**
     * @param type type of subscription
     * @param i instrument
     *
     * @note if there is no subscription, it does nothing
     *
     * @note your strategy doesn't need to call unsubscribe in the destructor.
     *
     */
    MT_UNSAFE void unsubscribe(MarketEventType type, const Instrument &i) {
        _ctx->unsubscribe(type, i);
    }


    ///Awaitable to receive next market event
    /**
     * @return awaitable, which returns MarketEvent
     */
    MT_UNSAFE Awaitable<MarketEvent> receive_event() {
        return [this](auto &&cb) {
            _ctx->on_market_event(std::move(cb));
        };
    }

    ///Request one-shot market update
    /**
     * Request to market update for given instrument and type. It is useful, when strategy
     * needs update in periodic interval, which is longer than few seconds. For example, if
     * the strategy runs on 5m chart, it probably needs to price data only once per 5 minutes
     *
     * You should avoid to use this function too often. if you need short period of update,
     * use subscribe()
     *
     * @param type type of market data
     * @param i instrument
     * @return function returns an awaiter, because operation is asynchronous. You can
     * attach a callback, which accepts const AsyncStatus& and const MarketEvent &. If
     * the awaiter is used in coroutine, the co_await expression returns MarketEvent
     *
     * @code
     * update_market(MarketEventType::tickdata, bitcoin) >> [this](
     *          const AsyncStatus &st, const MarketEvent &event) {
     *          if (st) {
     *              std::optional<TickData> ticker = event;
     *              if (ticker) {
     *                //process ticker
     *              } else {
     *                  //error
     *                }
     *          } else {
     *              //async error
     *          }
     * };
     * @endcode
     *
     * @code
     * std::optional<TickData> me = co_await update_market(MarketEventType::tickdata, bitcoin);
     * if (me) {
     *      //process ticker
     * } else {
     *      //error
     * }
     * @endcode
     */
    MT_UNSAFE Awaitable<MarketEventData> update_market(MarketEventType type, const Instrument &i) {
        return [this, type, &i](auto &&promise) {
            _ctx->update_market(i, type, std::move(promise));
        };
    }



    ///Retrieve logger object (for logging and output)
    MT_SAFE Log get_logger() {return _ctx->get_logger();}

    ///Subscribe MQ channel
    /**
     * MQ service allows to communicate between strategies. This function
     * subscribes an MQ channel and allows to listen messages on that channel.
     * The messages are received through on_mq_message() callback
     *
     * @param channel channel name. Name can't be empty string
     */
    MT_UNSAFE void subscribe_channel(std::string_view channel) {
        _ctx->subscribe_channel(channel);
    }

    ///Unsubscribe MQ channel
    /**
     * @param channel channel to unsubscribe
     */
    MT_UNSAFE void unsubscribe_channel(std::string_view channel) {
        _ctx->unsubscribe_channel(channel);
    }

    ///Send message to a channel
    /**
     * @param channel name of channel. You are allowed to send messages to channel
     *  you are not subscribed. You can use sender name as channel name to send
     *  direct message
     * @param msg message to send
     *
     * @note there is no way how to find out whether the message was delivered
     */
    MT_UNSAFE void send_message(std::string_view channel, std::string_view msg, std::uint32_t cid = 0) {
        _ctx->send_message(channel, msg, cid);
    }


    ///Awaits on next MQ message.
    /**
     * There can be multiple awaiting coroutines.
     *  All of them receives the very next message
     * @return awaitable object
     */
    MT_UNSAFE Awaitable<Message> receive_message() {
        return [this](auto &&cb) {
            _ctx->on_mq_message(std::move(cb));
        };
    }


    ///Schedule operation on idle cycle
    /**
     * @return awaitable. You can use co_await or operator >> to a enqueue callback
     *
     * You can schedule multiple idle calls. They will consume more idle cycles
     *
     * @note idle has lowest priority. Which also means, that market events, order
     * updates and even timers can be processed between each idle cycle
     *
     * You can use `co_await on_idle()` in coroutine, if you need to retrieve latest
     * data. However, do not waste CPU time by calling this function in cycle
     *
     * @note function is MT-Safe. If called from different thread, it is enqueued
     * in first on idle event
     *
     */
    MT_SAFE Awaitable<void> on_idle() {
        return [this](auto &&cb) {
            _ctx->on_idle(std::move(cb));
        };
    }

    ///Get extension service
    /**
     * Services can extends features in future versions. You only need to known
     * service type (wrapper type). If the service is supported, function returns
     * initialized service wrapper. If th service is not supported, than
     * wrapper is returned in uninitialized state
     *
     * @tparam T type of service/service wrapper
     * @return initialized wrapper
     *
     * @code
     * auto new_feature_service = get_service<NewFeature>();
     * if (new_feature_service) {
     *          //service is supported
     * } else {
     *          //service is not supported
     * }
     * @endcode
     */
    template<typename T>
    MT_UNSAFE T get_service() {
        return _ctx->get_service<T>();
    }

    /// Append a point to a series
    /**
     * @param series_name Name of the series
     * @param point_data Binary representation of a point (serialized to binary)
     * @return Index of the newly created point
     *
     * @see Series
     */
    MT_UNSAFE std::uint64_t series_add_point(std::string_view series_name, std::string_view point_data) {
        return _ctx->series_add_point(series_name, point_data);
    }

    /// Erase older points from a series
    /**
     * @param series_name Name of the series
     * @param index_and_less Highest index of points to erase
     *
     * @see Series
     */
    MT_UNSAFE void series_erase_points(std::string_view series_name, std::uint64_t index_and_less) {
        _ctx->series_erase_points(series_name, index_and_less);
    }

    ///Loads points of series
    /**
     * @param name
     * @return instance of Values - iteratable container of values in
     * binary serialized format for given series
     *
     * @see Series
     */
    MT_UNSAFE ValueStream<std::string_view> load_series(std::string_view name) const {
        return _ctx->load_series(name);
    }


    ///Stop the strategy (now)
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

    MT_UNSAFE void stop() {
        _ctx->stop();
    }

    ///returns true, if stop has been requested
    /**
     * @retval false normal operation
     * @retval true stop has been requested
     */
    MT_UNSAFE bool is_stop_requested() const {
        return _ctx->is_stop_requested();
    }

    ///Awaits for stop request
    /** A callback or coroutine is executed, when stop is requested.
     *
     * The strategy should finish its operation and call stop() if everything
     * is settled. If no such callback/coroutine is defined, the strategy
     * is stopped immediately on the request
     *
     * @code
     * on_stop_requested() >> [&]{
     *      //handle operation
     * }
     * @endcode
     */
    MT_UNSAFE Awaitable<void> on_stop_requested() {
        return [this](auto &&promise) {
            _ctx->on_stop_requested(std::move(promise));
        };
    }

protected: //optional overrides
    ///called on market event
    /**
     * This function is called on any market event before any registered subscription.
     * @param i instrument
     * @param ev market event
     * @note you need to call the original implementation in order to call all registered callbacks
     */
    MT_UNSAFE virtual void on_market_event(const MarketEvent &) override {}
    ///Called when a change of order status occurs
    /**
     * @param ord order reference
     * @note you need to call the original implementation in order to call all registered callbacks
     */
    MT_UNSAFE virtual void on_order_report(Order , std::span<Fill> ) override {}

    MT_UNSAFE virtual void on_start() override {
        while (!_start_cbs.send());
        _started = true;
        main();
    }



protected:
    MT_UNSAFE Log log;

private:
    IContext *_ctx = nullptr;


    virtual void on_init(IContext *ctx) override {
        _ctx = ctx;
        log = _ctx->get_logger();
    }

    Signaller<void>  _start_cbs;

    bool _started = false;

};


}
///Export the strategy, so the strategy can be loaded by the loader
/**
 * @param class_name name of the strategy (class name). The strategy is
 * registered under its name
 */
#define EXPORT_STRATEGY(class_name) ::quarkbot::IModule::Factory<IStrategy> strategy_reg_##class_name(#class_name, std::in_place_type<class_name>)

///Export the strategy, but specify other name
/**
 * @param class_name class name of strategy
 * @param export_name exported name
 */
#define EXPORT_STRATEGY_AS(class_name, export_name) ::quarkbot::IModule::Factory<IStrategy> strategy_reg_##class_name(export_name, std::in_place_type<class_name>)








