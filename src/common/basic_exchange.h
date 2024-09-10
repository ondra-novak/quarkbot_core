#pragma once

#include "event_target.h"
#include "small_set.h"
#include "dispatcher.h"
#include "icontrol.h"

#include <quarkbot/strategy_context.h>
#include <quarkbot/iexchange.h>
#include <unordered_map>
#include <mutex>


namespace quarkbot {

class BasicExchangeContext: public IExchangeContext,
                            public IExchangeInfo,
                            public IControlledEntity,
                            public std::enable_shared_from_this<BasicExchangeContext> {
public:

    using GlobalScheduler = Function<void(Timestamp,Function<void(Timestamp)>, const void *)>;


    using Query = IExchange::Query;


    BasicExchangeContext(std::string label,
            IControl &control,
            Network ntw,
            Log log);


    void init(std::unique_ptr<IExchange> svc, Config configuration);

    void set_api_key(std::string_view name, const Config &api_key_config);

    void unset_api_key(std::string_view name);


    ///Disconnect given event target
    /**
     * Causes that any objects associated with this event target are disposed.
     * This includes open orders, subscriptions, pending updates, etc
     * @param target target to disconnect
     *
     * @b implementation: target must be removed from all subscriptions and
     * asynchronous operations
     */
    void disconnect(const IEventTarget *target);

    ///restore orders
    /**
     * Each order is stored in the database as its binary form. The serialization to
     * binary form is performed by the exchange through Order::to_binary(). At
     * the begin of strategy, all stored orders must be restored and associated instances
     * recreated. This function is responsible for such operation.
     *
     * The strategy should at least remember an unique identifier of the order.
     *
     * This function can be asynchronous. Multiple pending requests are possible at the time
     * (so it is allowed to call this function even if previous call is not done yet)
     *
     * @param acc account specifies account which should own these orders.
     * @param orders list of orders in binary form
     * @param collector reference to instance which receives orders and all fills
     *
     * @note function can be asynchronous but it is allowed to execute it synchronously.
     */
    void restore_orders(const Account &acc, std::span<SerializedOrder> orders, IExchange::RestoreOrdersCallback collector);
    ///Request to subscribe market data (stream)
    /**
     * @param target object which consumes updates
     * @param sbstype type of subscription
     * @param instrument instrument which is subscribed
     */
    void subscribe(IEventTarget *target, MarketEventType sbstype, const Instrument &instrument);
    ///Unsubscribe stream
    /**
     * @param target object which consumes updates
     * @param sbstype type of subscription
     * @param instrument instrument
     */
    void unsubscribe(IEventTarget *target, MarketEventType sbstype, const Instrument &instrument);

    ///Create order instance - don't place order yet
    /**
     * @param instrument associated instrument
     * @param setup order setup
     * @return pointer to newly created order. In case of error, discarded order must be created
     *
     * @b implementation: The function must create own instance of the order. Default implementation
     * creates BasicOrder instance. It must not place the order
     */
    Order create_order(const Instrument &instrument, const Account &account, const Order::Setup &setup, std::string_view label);

    ///Create order instance which replaces other order - don't place order yet
    /**
     * @param replace order to replace
     * @param setup order setup
     * @param amend try to amend order
     * @return pointer to newly created order. In case of error, discarded order must be created
     *
     * @b implementation: The function must create own instance of the order. Default implementation
     * creates BasicOrder instance. It must not place the order
     */
    Order create_order_replace(const Order &replace, const Order::Setup &setup, std::string_view label);

    ///Place orders in batch
    /**
     * @param target event target where these orders belongs
     * @param orders orders - must be created by create_order
     */
    void batch_place(IEventTarget *target, std::span<Order> orders);

    ///Cancel orders in batch
    /**
     * @param orders list of orders to cancel
     */
    void batch_cancel(std::span<Order> orders);
    ///Subscribe for other events
    /**
     * Need only if order has been restored, otherwise batch_place automatically subscribes
     * @param target target which receives events
     * @param order order. Ensure that order is not done
     */
    void subscribe_order(IEventTarget *target, const Order &order);

    ///Request update of ticker
    /**
     * You can request update of a current ticker in case that your strategy doesn't
     * subscribe on stream, but needs time to time check price of the instrument.
     * There can be limit how often the strategy can request to update ticker.
     * If you call this function too often, extra requests are dropped (there can
     * be rate limit defined by exchange) However the service provider still
     * must report completion of the operation (it can send the last known ticker data
     * in this case)
     *
     * Purpose of this function is to ask ticker in minute interval or more
     *
     * @param target
     * @param instrument
     */
    void update_ticker(IEventTarget *target, const Instrument &instrument);
    ///Request to update account
    /**
     * The account don't need to be tracked realtime. The strategy needs to
     * request update account state manually. There can be also limit
     * how often the update can be called. If the update is called too often
     * the request can be dropped (however the service provider must report
     * completion of the request)
     *
     * @note The service provider should update account before fill is reported.
     * However it don't need to update all informations, just informations related
     * to fill. It must for example update the position on the account.
     *
     * @param target
     * @param account
     */
    void update_account(IEventTarget *target, const Account &account);
    ///Request to update instrument
    /**
     * Updates instrument information.
     *
     * @param target
     * @param instrument
     */
    void update_instrument(IEventTarget *target, const Instrument &instrument);


    ///Retrieve exchange icon
    std::optional<IExchangeInfo::Icon> get_icon() const override;

    ///Get pointer to AbstractExchange from Exchange object
    static BasicExchangeContext &from_exchange(ExchangeInfo ex);

    virtual std::string get_name() const override;
    virtual std::string get_id() const override;

    ///Applies report to order object
    /**
     * Order object is owned by strategy and can be accessed anytime during processing,
     * because there is no locking scheme. This means, that new order state must
     * be applied synchronously with the strategy. This function is called in
     * strategy thread before the order is passed to the strategy and allows to
     * apply report to the order's internal state.
     *
     * @param order subject
     * @param report report to apply
     *
     * @note the order must be created by this exchange, otherwise function can throw exception
     */
    void order_apply_report(const Order &order, const Order::Report &report);


    void load_credentials(const Config &credential_config, std::string_view label, Function<void(ExchangeCredentials)> result);
    void query_accounts(const ExchangeCredentials &creds, const Query &query,std::string_view label,Function<void(std::span<Account>)> result);
    void query_instruments(const ExchangeCredentials &creds,const Query &query, std::string_view label,Function<void(std::span<Instrument>)> result);
    void query_instruments(const Query &query,std::string_view label,Function<void(std::span<Instrument>)> result);


    virtual ExchangeInfo get_exchange_info() const override;
    virtual Log get_log() const override;
    virtual Network get_network() const override;

    void update_market(IEventTarget *, const Instrument &i, MarketEventType type);

    virtual const Config &get_config() const override;
    virtual void set_timer(Timestamp at, TimerCallback fnptr, TimerID id = 0) override;
    virtual bool clear_timer(TimerID id) override;

protected:

    struct Subscription {
        MarketEventType type;
        Instrument i;

        bool operator==(const Subscription &) const = default;
        std::strong_ordering operator<=>(const Subscription &) const = default;
    };

    struct SubscriptionHasher {
        std::size_t operator()(const Subscription &x) const {
            Instrument::Hasher hasher;
            std::size_t h = hasher(x.i);
            return h + static_cast<int>(x.type);
        }
    };



    ///Object's lock, derived class must use this lock to lock internals
    mutable std::recursive_mutex _mx;

    std::string _label;
    IControl &_control;
    Network _ntw;
    Log _log;
    Config _cfg;




    std::unordered_map<Order, IEventTarget *, Order::Hasher> _orders;

    std::unordered_map<Subscription, SmallSet<IEventTarget *> , SubscriptionHasher> _subscriptions;
    std::unordered_map<Subscription, SmallSet<IEventTarget *> , SubscriptionHasher> _market_updates;
    std::unordered_map<Instrument, SmallSet<IEventTarget *> , Instrument::Hasher> _instrument_update_waiting;
    std::unordered_map<Account, SmallSet<IEventTarget *> , Account::Hasher> _account_update_waiting;

    std::mutex _queue_mx;
    DispatcherCore<TimerCallback> _queue;
    bool _processing_queue = false;
    bool _is_stopped = false;
    bool _stop_requested = false;
    Function<void()> _request_stop_cb = {};


    virtual bool income_data(const MarketEvent &event) override;
    ///call this function when account is updated
    virtual void object_updated(const Account &i, AsyncResult<void> st) override;
    ///call this function when instrument is updated
    virtual void object_updated(const Instrument &i, AsyncResult<void> st) override;

    virtual void object_updated(const Instrument &i, MarketEventType type, AsyncResult<MarketEventData> ev) override;


    virtual void order_report(const Order &order, Order::Report report) override;

    virtual std::string get_label() const override;


private:

    std::unique_ptr<IExchange> _ptr;

    virtual void on_scheduled(Timestamp tp) noexcept override;
    virtual bool is_stopped() const noexcept override;
    virtual void request_stop() noexcept override;
    virtual void on_stop_requested(Function<void()> &&cb) override;
    virtual void stop() override;
void notify_queue();


};


}
