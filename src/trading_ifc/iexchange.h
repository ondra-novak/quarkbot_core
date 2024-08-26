#pragma once

#include "exchange_context.h"
#include "exchange_credentials.h"

#include "config_desc.h"
namespace trading_api {



///Implements exchange features - exchange implementation
class IExchange {
public:

    using Query = Config;



    virtual ~IExchange() = default;

    ///Retrieve configuration schema for configuration needed to initialize the instance
    /**
     * this function can be called without init();
     * @return configuration schema
     */
    virtual ConfigSchema get_exchange_config_schema() const = 0;

    ///Retrieve configuration schema to configure api key fields
    /**
     * this function can be called without init()
     * @return
     */
    virtual ConfigSchema get_api_key_config_schema() const = 0;

    ///Called to init exchange service
    /**
     * @param context exchange context
     * @param config optional configuration - probably you will need to pass
     * an API key through this
     */
    virtual void init(IExchangeContext *context) = 0;


    ///Load credentials
    /**
     * Function loads and checks credential validity. If the credentials are valid
     * it returns initialized ExchangeCredentials object. Otherwise it returns uninitialized
     * object.
     *
     * @param credential_config defines all fields required to perform success loging (api key etc)
     * @param label associated label (text)
     * @param result a function, which is called upon completion. The function is asynchronous,
     *
     */
    virtual void load_credentials(const Config &credential_config, std::string_view label, Function<void(ExchangeCredentials)> result) = 0;


    ///Query for accounts
    /**
     * @param creds credentials (see load_credentials)
     * @param label associated label (text)
     * @param query contains fields required to match specified account (exchange depend)
     * @param result a function, which is called upon a completion. The function
     * retrieves list of accounts matching the query
     */
    virtual void query_accounts(const ExchangeCredentials &creds,
            std::string_view label, const Query &query,
            Function<void(std::span<Account>)> result) = 0;

    ///Query for exclusive instruments
    /**
     * The exchange can opt to not offer public instruments. It can require a login to
     * retrieve list of instruments
     *
     * @param creds credentials
     * @param query
     * @param label
     * @param result
     */
    virtual void query_instruments(const ExchangeCredentials &creds,
            const Query &query, std::string_view label,
            Function<void(std::span<Instrument>)> result) = 0;

    ///Query for public instruments
    /**
     * @param query
     * @param label
     * @param result
     */
    virtual void query_instruments(const Query &query,
            std::string_view label,
            Function<void(std::span<Instrument>)> result) = 0;


    ///Subscribe instrument
    /** Subscribe this object to market data for given instrument
     *
     * @param type type of subscription
     * @param i instrument
     *
     * Market data are passed through income_data()
     *
     * @note multiple subscriptions should be ignored
     * @note called under lock
     */
    virtual void subscribe(MarketEventType type, const Instrument &i) = 0;
    ///Unsubscribe instrument
    /** Unsubscribe this object from market data for given instrument
     *
     * @param type type of subscription
     * @param i instrument
     *
     * @note multiple unsubscptions should be ignored
     * @note called under lock
     */
    virtual void unsubscribe(MarketEventType type, const Instrument &i) = 0;
    ///Request update account object
    /**
     * @param a account to update
     * @note when update is complete, call object_updated
     * @note called under lock
     */
    virtual void update_account(const Account &a) = 0;
    ///Request update account object
    /**
     * @param i instrument to update
     * @note when update is complete, call object_updated
     * @note called under lock
     */
    virtual void update_instrument(const Instrument &i) = 0;

    ///Request one-shot market update
    /**
     * @param i instrument
     * @param type type
     *
     * for example, request for tickdata can perform REST request for current ticker
     * @note control object doesn't call this function for subscribed instruments
     */
    virtual void update_market(const Instrument &i, MarketEventType type) = 0;
    ///Place orders on exchange
    /**
     * @param orders list of orders
     * @note order report is passed through order_fill or order_restore
     * @note called under lock
     */
    virtual void batch_place(std::span<Order> orders) = 0;

    ///Cancel orders
    /**
     * @param orders list of orders to cancel
     * @note canceled orders should receive status canceled after successfuly canceled
     *
     */
    virtual void batch_cancel(std::span<Order> orders) = 0;

    ///Get exchange human readable name
    virtual std::string get_name() const = 0;
    ///Get exchange unique identifier
    virtual std::string get_id() const  = 0;

    ///Get exchange's icon (optional)
    virtual std::optional<IExchangeInfo::Icon> get_icon() const = 0;

    ///Create order base on request
    /**
     * Implementation of the order is internal issue of the exchange service
     * @param instrument associated instrument
     * @param account associated account
     * @param setup order configuration
     * @param label user defined custom label, can be later read by get_label(). Label
     * should be also serialized into binary form
     * @return created order.
     * @note if order cannot be created, the function should create error order,
     * which is returned in discarded state.
     */
    virtual Order create_order(const Instrument &instrument, const Account &account, const Order::Setup &setup, std::string_view label) = 0;


    ///Create order which replaces other order
    /**
     * @param replace order to replace
     * @param setup setup of new order
     * @param label user defined custom label, can be later read by get_label(). Label
     * should be also serialized into binary form
     * @return created order.
     * @note if order cannot be created, the function should create error order,
     * which is returned in discarded state.
     */
    virtual Order create_order_replace(const Order &replace, const Order::Setup &setup, std::string_view label) = 0;

    using RestoredOrders = std::span<std::pair<Order, Order::Report> >;
    using RestoreOrdersCallback = Function<void(AsyncResult<RestoredOrders>)>;

    ///restore orders
    /**
     * Each order is stored in the database as its binary form. The serialization to
     * binary form is performed by the exchange through Order::to_binary(). At
     * the begin of strategy, all stored orders must be restored and associated instances
     * recreated. This function is responsible for such operation.
     *
     * The strategy should at least remember an unique identifier of the order.
     *
     * @param acc account specifies account which should own these orders.
     * @param orders list of orders in binary form
     * @param callbalback, this callback is called with orders and their repost, which
     * can contains restored fills. The order instance must be in "restored"state and
     * the report could contain its actuall state. The strategy will call order_apply_report
     * for each this state
     *
     * @note function can be asynchronous but it is allowed to execute it synchronously.
     */
    virtual void restore_orders(const Account &acc,
            std::span<SerializedOrder> orders,
            RestoreOrdersCallback callback) = 0;

    ///Applies report to order object
    /**
     * Order object is owned by strategy and can be accessed anytime during processing,
     * because there is no locking scheme. This means, that new order state must
     * be applied synchronously with the strategy. This function is called in the
     * strategy thread before the order is passed to the strategy event handler
     * and it is used to apply report to the order's internal state, so the report
     * becomes visible in order state. The exchange should avoid to change
     * order state in different place (or thread). This reason, why Order::Report exists
     *
     *
     * @param order subject
     * @param report report to apply
     *
     * @note the order must be created by this exchange, otherwise function can throw exception
     */
    virtual void order_apply_report(const Order &order, const Order::Report &report)  =0;



};
}
