#pragma once
#include "istrategy.h"
#include "awaiter.h"

#include <queue>

namespace trading_api {

///Strategy base object - extend this object to create own strategy
class Strategy: public IStrategy {
public:



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
    virtual ConfigSchema get_config_schema() const override {
        return {};
    }

    ///called before on_start() if there are active orders (from previous run)
    /**
     * @param active_orders active orders. All orders have restored state. They
     * actuall state is set through standard report which is send after on_start()
     */
    virtual void on_active_orders(ActiveOrders active_orders) {
        log.warning("called pure virtual on_active_orders() - you should override");
    }

    ///this is strategy's entry point. You need to declare own implementation
    /**
     * Function is called as an event, but only once at the beginning. In this
     * case, everything is correctly initialized and strategy can do anything it
     * wants.
     *
     * If there are stored opened orders, they report arrives immediatelly after
     * strategy finishes on_start(). If you need a special processing after these
     * reports arrive, you can use on_idle() to schedule execution
     */
    virtual void on_start() override {
        log.warning("called pure virtual on_start() - you should override");

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
    virtual void on_unhandled_exception() override {
        throw;
    }



public:  //context API

    ///Retrieve available accounts
    /**
     * @return available (configired) accounts
     */
    std::span<const Account> get_accounts() const {return _ctx->get_accounts();}

    ///Retrieve available instruments
    /**
     * @return configired instruments
     */
    std::span<const Instrument> get_instruments() const {return _ctx->get_instruments();}

    ///Retrieve strategy's configuration
    /**
     * @return configuration object
     */
    const Config &get_config() const {return _ctx->get_config();}

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
    void set_var(std::string_view key, std::string_view value) const {
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
    void set_var(std::string_view key, const T &value) const {
        std::string s;
        Serializer::to_binary(std::back_inserter(s), value);
        _ctx->set_var(key,s);
    }

    ///unset variable
    /**
     * @param varname variale to unset
     * @note it is better to use unset_var instead of setting variable to empty string.
     */
    void unset_var(std::string_view varname) const {
        _ctx->unset_var(varname);
    }

    ///retrieve stored value from database
    /**
     * @param varname name of variable
     * @return content. If the variable doesn't exist, returns empty string
     */
    std::string get_var(std::string_view varname) const{
        return _ctx->get_var(varname);
    }

    ///retrieve stored value
    /**
     * @param key variable name
     * @param default_value default value in case that variable is undefined
     * @return content of variable or default value if doesn't exists
     */
    template<SerializableType T>
    T get_var(std::string_view varname, const T &default_value) const{
        static_assert(!std::is_same_v<T, std::string_view>, "Can't return reference, use std::string");
        std::string s = _ctx->get_var(varname);
        if (s.empty()) return default_value;
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
    VarSet<T> get_vars(std::string_view prefix) const {
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
    VarSet<T> get_vars(std::string_view from_key, std::string_view to_key) const {
        return _ctx->get_vars(from_key, to_key);
    }

    ///unset multiple variables
    /**
     * @param vars table of variables
     * @note you can delete whole table
     */
    template<typename T>
    void unset_vars(const VarSet<T> &vars) const {
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
    Awaitable<void> update_account(const Account &a)  {
        _ctx->update_account(a);
        return [this, a](auto &&cb){
            _update_account_cbs[a].emplace_back(std::move(cb));
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

    Awaitable<void> update_instrument(const Instrument &i)  {
        _ctx->update_instrument(i);
        return [this, i](auto &&cb){
            _update_instrument_cbs[i].emplace_back(std::move(cb));
        };
    }


    ///Retrieves time of current event
    /**
     * @return time of current event (time when this event started). Note that
     * the function returns same value during processing the event, so don't
     * use it to measure time of calculations
     */
    Timestamp get_event_time() const {return _ctx->get_event_time();}

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
    Awaitable<void> wait_until(Timestamp at, TimerID id = 0) {
        return [this, at, id](auto &&cb) {
            _ctx->set_timer(at, [cb = std::move(cb)]{
                cb(AsyncResult<void>(std::in_place));
            });
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
    Awaitable<void> wait_for(std::chrono::duration<A,B> dur, TimerID id = 0) {
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
    bool interrupt_wait(TimerID id) {return _ctx->clear_timer(id);}
    ///Place an order
    /**
     * @param account account used to trade the instrument
     * @param instrument instrument on where order will be placed
     * @param setup order type and configuration
     * @param label custom label (don't need to be unique). The label can be retrieves by get_label()
     * @return created order
     */
    Order place_order(const Account &account, const Instrument &instrument, const Order::Setup &setup, std::string_view label = {}) {
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
    Order bind_order(const Account &account, const Instrument &instrument, std::string_view label) const {
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
    void cancel_order(const Order &order) const {_ctx->cancel(order);}

    ///Replace order
    /**
     * @param order order to replace.
     * @param setup new setup of the order
     * @param label custom label. If this value is empty, the function uses label of the
     * order being replaced.
     *
     * @return new order
     */
    Order replace_order(const Order &order, const Order::Setup &setup, std::string_view label)  {
        return _ctx->replace(order, setup, label.empty()?order.get_label():label);
    }



    ///Request awaiter for order report
    /**
     * @param order order
     * @return awaiter. Awaiter can be used to co_await, or you can attach a callback.
     * Note this is one-shot action - only first report is received by callback, you
     * need to request report again. Result of co_await operation is fills. This
     * array can be empty, which means, that only status changed, without fills
     * @note there can be only one awaiter per order. Multiple awaiters cancels each other
     */
    Awaitable<Fills> on_report(const Order &order) {
        if (order.done()) {
            throw std::runtime_error("Order already done (order.done() == true)");
        } else {
            return [this, order](auto &&cb) {
                _order_report[order].emplace_back(std::move(cb));
            };
        }
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
    Fills get_fills(std::size_t limit, std::string_view filter = {}) const {
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
    Fills get_fills(Timestamp tp, std::string_view filter = {}) const {
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
    Positions get_opened_positions(std::string_view filter = {}) const {
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
    Trades get_closed_positions(Timestamp limit, std::string_view filter = {}) const {
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
    void allocate_equity(const Account &a, double equity) {
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
    void subscribe(MarketEventType type, const Instrument &i) {
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
    void unsubscribe(MarketEventType type, const Instrument &i) {
        _ctx->unsubscribe(type, i);
    }


    ///Awaitable to receive next market event
    /**
     * @return awaitable, which returns pair, containing instrument and market event.
     * This function is introduced to help write strategies as coroutines.
     * You can call receive_event multiple times, all awaiters will receive the next event
     */
    Awaitable<std::pair<Instrument, MarketEvent> > receive_event() {
        return [this](auto &&cb) {
            _receive_market_event_cbs.emplace_back(std::move(cb));
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
    Awaitable<MarketEvent> update_market(MarketEventType type, const Instrument &i) {
        _ctx->update_market(i, type);
        return [this, i, type](auto &&cb) {
            _update_market_cbs[InstSubPair(i, type)].emplace_back(std::move(cb));
        };
    }

    ///Retrieve logger object (for logging and output)
    Log get_logger() {return _ctx->get_logger();}

    ///Subscribe MQ channel
    /**
     * MQ service allows to communicate between strategies. This function
     * subscribes an MQ channel and allows to listen messages on that channel.
     * The messages are received through on_mq_message() callback
     *
     * @param channel channel name. Name can't be empty string
     */
    void subscribe_channel(std::string_view channel) {
        _ctx->mq_subscribe_channel(channel);
    }

    ///Unsubscribe MQ channel
    /**
     * @param channel channel to unsubscribe
     */
    void unsubscribe_channel(std::string_view channel) {
        _ctx->mq_unsubscribe_channel(channel);
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
    void send_message(std::string_view channel, std::string_view msg) {
        _ctx->mq_send_message(channel, msg);
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
     */
    Awaitable<void> on_idle() {
        return [this](auto &&cb) {
            _on_idle_cbs.push_back(std::move(cb));
        };
    }

    ///Schedule operation on idle cycle after next event
    /**
     * This schedules operation to be executed in idle cycle after next event.
     *  - event : on_idle(A)
     *  - event : on_next_event(B)
     *  - A is executed : on_idle(C), on_next_event(D)
     *  - C is executed :
     *  - event : on_idle(E)
     *  - event : on_next_event(F)
     *  - B is executed :
     *  - E is executed :
     *  - event
     *  - F is executed :
     *
     * @return awaitable. You can co_await or attach a callback
     */
    Awaitable<void> on_next_event() {
        return [this](auto &&cb) {
            _on_next_event_cbs.push_back(std::move(cb));
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
    T get_service() {
        return _ctx->get_service<T>();
    }




protected: //optional overrides
    ///called on market event
    /**
     * This function is called on any market event before any registered subscription.
     * @param i instrument
     * @param ev market event
     * @note you need to call the original implementation in order to call all registered callbacks
     */
    virtual void on_market_event(const Instrument i, MarketEvent ev) override {
        invoke_callbacks(_receive_market_event_cbs, std::pair<Instrument,MarketEvent>(std::move(i), ev));
    }
    ///Called when a change of order status occurs
    /**
     * @param ord order reference
     * @note you need to call the original implementation in order to call all registered callbacks
     */
    virtual void on_order_report(const Order ord, std::vector<Fill> fills) override {
        auto iter = _order_report.find(ord);
            if (iter != _order_report.end() && invoke_callbacks(iter->second, std::move(fills))) {
        }
    }
    ///It is called when an MQ message arrives on the subscribed channel or in the private mailbox
    /**
     * @param msg MQ message
     * @note you need to call the original implementation in order to call all registered callbacks
     */
    virtual void on_mq_message(const Message &) override {};
    ///Called when there are no events
    /**
     * @note you need to call the original implementation in order to call all registered callbacks
     *
     */
    virtual bool on_context_idle() override {
        //any idle?
        if (!_on_idle_cbs.empty()) {
            //retrieve front and execute it
            _on_idle_cbs.front()(true);
            //pop it
            _on_idle_cbs.pop_front();
            //indicate that we are not complete
            return false;
        } else {
            //no more idle
            //prepare on_next_event callbacks to be scheduled on next on_idle
            std::swap(_on_idle_cbs, _on_next_event_cbs);
            //indicate completion, so new idle list won't be called
            return true;
        }
    }
    ///
    /**
     * @param a account updated
     * @param status update status
     *
     * @note you need to call the original implementation in order to call all registered callbacks
     *
     */
    virtual void on_update_complete(const Account &a, AsyncResult<void> result) override {
        auto iter = _update_account_cbs.find(a);
        if (iter != _update_account_cbs.end() && invoke_callbacks(iter->second,std::move(result))) {
            _update_account_cbs.erase(iter);
        }
    }
    /**
     * @param i instrument updated
     * @param status update status
     *
     * @note you need to call the original implementation in order to call all registered callbacks
     *
     */
    virtual void on_update_complete(const Instrument &i, AsyncResult<void> result) override {
        auto iter = _update_instrument_cbs.find(i);
        if (iter != _update_instrument_cbs.end() && invoke_callbacks(iter->second,std::move(result))) {
            _update_instrument_cbs.erase(iter);
        }
    }

    virtual void on_update_complete(const Instrument &i, MarketEventType type, AsyncResult<MarketEvent> result) override {
        auto iter = _update_market_cbs.find(InstSubPair(i, type));
        if (iter != _update_market_cbs.end() && invoke_callbacks(iter->second,std::move(result))) {
            _update_market_cbs.erase(iter);
        }
    }

protected:
    Log log;

private:
    IContext *_ctx = nullptr;

    using InstSubPair = std::pair<Instrument, MarketEventType>;
    struct InstSubPairHasher {
        std::size_t operator()(const InstSubPair &p) const {
            return Instrument::Hasher()(p.first);
        }
    };

    virtual void on_init(IContext *ctx) override {
        _ctx = ctx;
        log = _ctx->get_logger();
    }

    std::unordered_map<Account, CallbackList<void>, Account::Hasher> _update_account_cbs;
    std::unordered_map<Instrument, CallbackList<void>,Instrument::Hasher> _update_instrument_cbs;
    std::unordered_map<InstSubPair, CallbackList<MarketEvent>,InstSubPairHasher> _update_market_cbs;
    std::unordered_map<Order, CallbackList<Fills>,Order::Hasher> _order_report;
    CallbackList<void>  _on_idle_cbs;
    CallbackList<void>  _on_next_event_cbs;
    CallbackList<std::pair<Instrument,MarketEvent> > _receive_market_event_cbs;

    template<typename CBList, typename Result>
    bool invoke_callbacks(CBList &lst, Result res) {
        std::size_t cnt = lst.size();
        for (std::size_t i = 0; i <cnt; ++i) {
            lst.front()(res);
            lst.pop_front();
        }
        return lst.empty();
    }

};


}
///Export the strategy, so the strategy can be loaded by the loader
/**
 * @param class_name name of the strategy (class name). The strategy is
 * registered under its name
 */
#define EXPORT_STRATEGY(class_name) ::trading_api::IModule::Factory<IStrategy> strategy_reg_##class_name(#class_name, std::in_place_type<class_name>)

///Export the strategy, but specify other name
/**
 * @param class_name class name of strategy
 * @param export_name exported name
 */
#define EXPORT_STRATEGY_AS(class_name, export_name) ::trading_api::IModule::Factory<IStrategy> strategy_reg_##class_name(export_name, std::in_place_type<class_name>)








