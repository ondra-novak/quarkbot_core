#pragma once

#include "../trading_ifc/strategy_context.h"
#include "../trading_ifc/iexchange.h"

#include "event_target.h"
#include "small_set.h"
#include <map>


namespace trading_api {

class BasicExchangeContext: public IExchangeContext, public IExchangeInfo, public std::enable_shared_from_this<BasicExchangeContext> {
public:

    using Query = IExchange::Query;


    BasicExchangeContext(std::string label, Network ntw, Log log);


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

    ///Request to update orders from exchange
    /**
     * Function is used to update state of orders stored in database
     *
     * @param target target which will consume update events. Impementation
     * must pass this pointer to order_restore()
     * @param orders list of orders to update. The orders are passed in binary
     * serialized form (because they are read from the database). The
     * exchange instance must use binary form to restore instances of Order class.
     * Then all these orders are passed as on_order event. If there are unprocessed
     * fills, the exachange must also generate on_fill()
     *
     * @b implementation: the service provider must know, which order can restore.
     * The binary representation must contain identification of the exchange. So
     * the service provider can restore orders which are known for the exchange.
     * other orders are order_restore(), then order_fill for every fill on the order,
     * and finally order_state_change with final order state.
     */
    void restore_orders(IEventTarget *target, std::span<SerializedOrder> orders);
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
    Order create_order(const Instrument &instrument, const Account &account, const Order::Setup &setup);

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
    Order create_order_replace(const Order &replace, const Order::Setup &setup, bool amend);

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
    virtual void order_apply_report(const Order &order, const Order::Report &report);


    ///Applies fill to order object
    /**
     * Because order object can be modified only synchronously with the strategy,
     * this function must be called in strategy thread.
     *
     * @param order order
     * @param fill fill
     *
     * @note the order must be created by this exchange, otherwise function can throw exception
     *
     */
    virtual void order_apply_fill(const Order &order, const Fill &fill);

    void load_credentials(const Config &credential_config, std::string_view label, Function<void(ExchangeCredentials)> result);
    void query_accounts(const ExchangeCredentials &creds,std::string_view label, const Query &query,Function<void(std::span<Account>)> result);
    void query_instruments(const ExchangeCredentials &creds,const Query &query, std::string_view label,Function<void(std::span<Instrument>)> result);
    void query_instruments(const Query &query,std::string_view label,Function<void(std::span<Instrument>)> result);


    virtual ExchangeInfo get_exchange_info() const override;
    virtual Log get_log() const override;
    virtual Network get_network() const override;

    void update_market(IEventTarget *, const Instrument &i, MarketEventType type);

    virtual const Config &get_config() const;

protected:

    ///Object's lock, derived class must use this lock to lock internals
    mutable std::recursive_mutex _mx;

    std::string _label;
    Network _ntw;
    Log _log;
    Config _cfg;


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



    std::unordered_map<Order, IEventTarget *, Order::Hasher> _orders;

    std::unordered_map<Subscription, SmallSet<IEventTarget *> , SubscriptionHasher> _subscriptions;
    std::unordered_map<Subscription, SmallSet<IEventTarget *> , SubscriptionHasher> _market_updates;
    std::unordered_map<Instrument, SmallSet<IEventTarget *> , Instrument::Hasher> _instrument_update_waiting;
    std::unordered_map<Account, SmallSet<IEventTarget *> , Account::Hasher> _account_update_waiting;


    virtual void income_data(const Instrument &i, const MarketEvent &t) override;
    ///call this function when account is updated
    virtual void object_updated(const Account &i, AsyncStatus st) override;
    ///call this function when instrument is updated
    virtual void object_updated(const Instrument &i, AsyncStatus st) override;

    virtual void object_updated(const Instrument &i, AsyncStatus st, MarketEvent ev) override;
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
    virtual void order_state_changed(const Order &order, const Order::Report &report) override;
    ///call this function for every fill on the order
    /**
     * @param order order instance
     * @param fill fill information
     */
    virtual void order_fill(const Order &order, const Fill &fill) override;
    ///call this function for every restored order from set passed to restore_orders()
    /**
     * @param target target argument passed to function restored_orders
     * @param order restored order instance
     */
    virtual void order_restore(void *target, const Order &order) override;

    virtual std::string get_label() const override;


private:

    std::unique_ptr<IExchange> _ptr;

};


}
