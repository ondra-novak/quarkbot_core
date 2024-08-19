#pragma once
#include "istrategy.h"

namespace trading_api {

///Strategy base object - extend this object to create own strategy
class Strategy: public IStrategy {
public:

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
    virtual void on_start() override {}

    ///called when order is filled (full or partially)
    /**
     * @param o order reference
     * @param f fill record
     * @return function should return a label of this fill. This fill is then
     * stored to the database with this label and allows later filtering. If you
     * don't using labels, return empty string
     *
     * @note if the exception is thrown during processing this function, the fill
     * is not stored into database!!!
     *
     */
    virtual std::string on_fill(const Order &o, const Fill &f) override {
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
    virtual void on_unhandled_exception() override {throw;}



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
    UpdateAwaiter<Strategy, Account> update_account(const Account &a)  {
        _ctx->update_account(a);
        return {this, a};
    }


    ///request update instrument
    /**
     * @param i instrument to update. The function refresh instrument features
     *  from the exchange
     * @param fn function which is called when update is complete
     *
     * @note you don't need to update instrument often.
     */

    UpdateAwaiter<Strategy,Instrument> update_instrument(const Instrument &i)  {
        _ctx->update_instrument(i);
        return {this, i};
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
    TimerAwaiter sleep_until(Timestamp at, TimerID id = 0) {
        return {_ctx, at, id};
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
    TimerAwaiter sleep_for(std::chrono::duration<A,B> dur, TimerID id = 0) {
        return {_ctx, get_event_time()+dur, id};
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
    bool interrupt_sleep(TimerID id) {return _ctx->clear_timer(id);}
    ///Place an order
    /**
     * @param instrument instrument on where order will be placed
     * @param account account used to trade on the instrument
     * @param setup order type and configuration
     * @return an object, which can be used to define callback and convert to Order instance
     *
     * @code
     * Order new_order = place_order(i,a,setup) >> [this](Order ord) {
     *      //handle order report
     * };
     * if (new_order.discarded()) {
     *      //failed to place
     * }
     * @endcode
     * @note setting callback function is optional. Without it, order reports are
     * passed through function on_order. If you override this function, you should
     * call original implementation.
     */
    OrderResult<Strategy> place_order(const Instrument &instrument, const Account &account, const Order::Setup &setup) {
        return {this,_ctx->place(instrument, account, setup)};
    }

    ///Creates an order, which is asociated with an instrument, but it is not placed
    /**
     * You can use replace() function to place the order with new setup. This
     * allows to track single order without need to know, whether order is actually
     * placed or not
     *
     * @param instrument associated instrument
     * @return dummy order (can be replaced)
     */
    Order bind_order(const Instrument &instrument, const Account &account) const {
        return _ctx->bind_order(instrument, account);
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
     * @param amend try to amend current order - the exchange just modifies amount,
     * limit or stop price if order is pending (waiting for trigger). So you can
     * amend only order with same side, instrument, etc. If amend is not possible,
     * because above rules, new order is discarded and original order is untouched.
     * If exchange doesn't support amend, the service provider can simulate this
     * feature. In all cases, filled amount is transfered to the new order.
     *
     * @return new order
     *
     * @note if replace partially filled order, filled amount is perserved
     * @note replace can fail, if exchange cannot garanteed to replace order
     * without avoiding double execution. In this case, old order is canceled
     * new order is rejected
     *
     * @note if order is pending, you probably can replace with order of same
     * side and behavior. If order is done, it can be replaced with any
     * order (it only associates with order's original instrument)
     */
    OrderResult<Strategy> replace_order(const Order &order, const Order::Setup &setup, bool amend)  {
        return {this,_ctx->replace(order, setup, amend)};
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
     * @return subcription control object. You can use operator >> to defined callback
     * function which handles market data. This is optional feature, otherwise
     * all market events are passed through function on_market_event()
     *
     * @code
     * subscribe(SubcriptionType::ticker, i) >> [this](Instrument i, MarketEvent ev) {
     *      //process market event
     * };
     * @endcode
     */
    Subscription<Strategy> subscribe(MarketEventType type, const Instrument &i) {
        _ctx->subscribe(type, i);
        return {this, type, i};
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
        remove_subscription(type, i);
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
    MarketEventAwaiter<Strategy> update_market(MarketEventType type, const Instrument &i) {
        _ctx->update_market(i, type);
        return {this, i, type};
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
    MQSubscription<Strategy> subscribe_channel(std::string_view channel) {
        _ctx->mq_subscribe_channel(channel);
        return {this, channel};
    }

    ///Unsubscribe MQ channel
    /**
     * @param channel channel to unsubscribe
     */
    void unsubscribe_channel(std::string_view channel) {
        _ctx->mq_unsubscribe_channel(channel);
        _mq_callbacks.erase(std::string(channel));
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

    ///Defines callback function for all messages sent directly to mailbox
    /**
     * @return subscription object
     *
     * @code
     * on_private_message() >> [this](const Message &msg) {
     *      auto sender = msg.get_sender();
     *      auto content = msg.get_content;
     *      //process message
     *      send_message(sender, response);
     * };
     * @endcode
     */
    MQSubscription<Strategy> on_private_message() {
        return {this, {}};
    }

    ///Enqueue operation on idle cycle
    /**
     * @return idle awaiter. You can use co_await or operator >> to a enqueue callback
     *
     * @code
     * on_idle() >> [this]{
     *      //process on idle
     * };
     * @endcode
     *
     * @code
     * co_await on_idle(); //suspend and continue on idle
     * @endcode
     *
     * You can enqueue multiple idle calls. They will consume more idle cycles
     */
    IdleAwaiter<Strategy> on_idle() {return this;}

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
    virtual void on_market_event(const Instrument &i, const MarketEvent &ev) override {
        handle_market_event(i, ev);
    }
    ///Called when a change of order status occurs
    /**
     * @param ord order reference
     * @note you need to call the original implementation in order to call all registered callbacks
     */
    virtual void on_order(const Order &ord) override {
        handle_callbacks(ord);
    }
    ///It is called when an MQ message arrives on the subscribed channel or in the private mailbox
    /**
     * @param msg MQ message
     * @note you need to call the original implementation in order to call all registered callbacks
     */
    virtual void on_mq_message(const Message &msg) override {
        handle_callbacks(msg);
    };
    ///Called when there are no events
    /**
     * @note you need to call the original implementation in order to call all registered callbacks
     *
     */
    virtual bool on_context_idle() override {
        if (!_on_idle_queue.empty()) {
            _on_idle_queue.front()();
            _on_idle_queue.pop();
        }
        return _on_idle_queue.empty();
    }
    ///
    /**
     * @param a account updated
     * @param status update status
     *
     * @note you need to call the original implementation in order to call all registered callbacks
     *
     */
    virtual void on_update_complete(const Account &a, const AsyncStatus &status) override {
        complete_update(_update_account_cbs, a, status);
    }
    /**
     * @param i instrument updated
     * @param status update status
     *
     * @note you need to call the original implementation in order to call all registered callbacks
     *
     */
    virtual void on_update_complete(const Instrument &i, const AsyncStatus &status) override {
        complete_update(_update_instrument_cbs, i, status);
    }

    virtual void on_update_complete(const Instrument &i, const AsyncStatus &status, const MarketEvent &event) override {
        InstSubPair key(i, event.get_type());
        if (status.get_status() == AsyncStatus::ok) {
            complete_update(_update_market_event_cbs, key, MarketEventStatus(event));
        } else {
            complete_update(_update_market_event_cbs, key, MarketEventStatus(status));
        }
    }


private:
    IContext *_ctx = nullptr;

    using InstSubPair = std::pair<Instrument, MarketEventType>;
    struct InstSubPairHasher {
        std::size_t operator()(const InstSubPair &p) const {
            return Instrument::Hasher()(p.first);
        }
    };


    virtual void on_init(IContext *ctx) override {_ctx = ctx;}
    std::unordered_map<InstSubPair, Function<void(const Instrument &, const MarketEvent &)>, InstSubPairHasher > _subscriptions_callbacks;
    std::unordered_map<Order, Function<void(const Order &) >, Order::Hasher > _order_callbacks;
    std::unordered_map<std::string, Function<void(MQClient::Message)> > _mq_callbacks;
    std::queue<Function<void()> > _on_idle_queue;
    std::vector<std::pair<Account, CompletionCB> > _update_account_cbs;
    std::vector<std::pair<Instrument, CompletionCB> > _update_instrument_cbs;
    std::vector<std::pair<InstSubPair, Function<void(const MarketEventStatus &)> > > _update_market_event_cbs;

    template<typename Fn>
    void add_callback(const Order &ord, Fn &&fn) {
        if (ord.discarded()) return;
        _order_callbacks[ord] = std::forward<Fn>(fn);
    }
    bool handle_callbacks(const Order &ord) {
        auto iter = _order_callbacks.find(ord);
        if (iter != _order_callbacks.end()) {
            iter->second(ord);
            if (ord.done()) _order_callbacks.erase(iter);
            return true;
        }
        return false;
    }
    template<typename Fn>
    void add_subscription(MarketEventType type, const Instrument &i, Fn &&fn) {
        _subscriptions_callbacks[InstSubPair(i, type)] = std::forward<Fn>(fn);
    }
    void remove_subscription(MarketEventType type, const Instrument &i) {
        _subscriptions_callbacks.erase(InstSubPair(i, type));
    }

    bool handle_market_event(const Instrument &i, const MarketEvent &ev) {
        auto iter = _subscriptions_callbacks.find(InstSubPair(i, ev.get_type()));
        if (iter != _subscriptions_callbacks.end()) {
            iter->second(i, ev);
            return true;
        }
        return false;
    }
    template<typename Fn>
    void add_mq_subscription(std::string channel, Fn &&fn) {
        _mq_callbacks[std::move(channel)] = std::forward<Fn>();
    }

    bool handle_callbacks(const Message &msg) {
        auto iter = _mq_callbacks.find(std::string(msg.get_channel()));
        if (iter != _mq_callbacks.end()) {
            iter->second(msg);
            return true;
        }
        return false;
    }
    template<typename Fn>
    void register_idle(Fn &&fn) {
        _on_idle_queue.push(std::forward<Fn>(fn));
    }
    template<typename A, typename B, typename Status>
    void complete_update(A &cbs, B &subj, const Status &st) {
        std::exception_ptr e = {};
        auto z = std::move(cbs);
        auto iter = std::remove_if(z.begin(), z.end(), [&](const auto &x){
            if (x.first == subj) {
                try {
                    x.second(st);
                } catch (...) {
                    e = std::current_exception();
                }
                return true;
            } else {
                return false;
            }
        });
        z.erase(iter, z.end());
        std::swap(z, cbs);
        for (auto &x: z) cbs.push_back(std::move(x));
        if (e) std::rethrow_exception(e);
    }
    template<std::invocable<AsyncStatus> CB>
    void register_update(const Account &a, CB &&cb) {
        _update_account_cbs.emplace_back(a, std::forward<CB>(cb));
    }
    template<std::invocable<AsyncStatus> CB>
    void register_update(const Instrument &i, CB &&cb) {
        _update_instrument_cbs.emplace_back(i, std::forward<CB>(cb));
    }
    template<std::invocable<AsyncStatus> CB>
    void register_update(const Instrument &i, MarketEventType type, CB &&cb) {
        _update_market_event_cbs.emplace_back(InstSubPair(i, type), std::forward<CB>(cb));
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








