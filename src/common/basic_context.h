#pragma once

#include "common.h"
#include "icontrol.h"
#include "event_target.h"
#include "dispatcher.h"
#include "storage.h"
#include "basic_exchange.h"
#include "icontrol.h"
#include "mq/mq.h"

#include <quarkbot/signal.h>


#include <quarkbot/strategy.h>
#include <deque>
#include <mutex>
#include <map>
#include <set>

namespace quarkbot {





class BasicContext: public IContext,
                    public IEventTarget,
                    public IMQBroker::IListener,
                    public IControlledEntity{
public:

    using GlobalScheduler = Function<void(Timestamp,Function<void(Timestamp)>, const void *)>;

    BasicContext(std::unique_ptr<IStorage> storage,
            IControl &control,
            Log logger,
            MQBroker mq,
            std::string_view strategy_name);

    BasicContext(const BasicContext &) = delete;
    BasicContext &operator=(const BasicContext &) = delete;

    ~BasicContext();


    void init(std::unique_ptr<IStrategy> strategy,
            std::vector<Account> accounts,
            std::vector<Instrument> instruments,
            Config config);

    ///request strategy to stop
    virtual void request_stop() noexcept override;
    ///returns true if strategy is stopped
    virtual bool is_stopped() const noexcept override;

    virtual void on_update(Instrument i, AsyncResult<void> st) override;
    virtual void on_update(Account a, AsyncResult<void> st)  override;
    virtual bool on_subscription_event(const MarketEvent &event)  override;
    virtual void on_update(Instrument i, MarketEventType type, AsyncResult<MarketEventData> ev)  override;
    virtual void on_order_report(Order order,Order::Report report)  override;
    virtual void subscribe(MarketEventType type, const Instrument &i)override;
    virtual void unsubscribe(MarketEventType type, const Instrument &i) override;
    virtual Order replace(const Order &order, const Order::Setup &setup, std::string_view label) override;
    virtual Fills get_fills(std::size_t limit, std::string_view filter = {}) const override;
    virtual Fills get_fills(Timestamp tp, std::string_view filter = {}) const override;
    virtual Order place(const Instrument &instrument, const Account &account,  const Order::Setup &setup, std::string_view label) override;
    virtual void cancel(const Order &order) override;
    virtual void set_timer(Timestamp at, TimerEventCB fnptr, TimerID id) override;
    virtual Timestamp get_event_time() const override;
    virtual Order bind_order(const Instrument &instrument, const Account &account, std::string_view label) override;
    virtual void update_account(const Account &a, Function<void(AsyncResult<void>)> &&cb) override;
    virtual void allocate(const Account &a, double equity) override;
    virtual bool clear_timer(TimerID id) override;
    virtual void update_instrument(const Instrument &i, Function<void(AsyncResult<void>)> &&cb) override;
    virtual void unset_var(std::string_view var_name) override;
    virtual void set_var(std::string_view var_name, std::string_view value) override;
    virtual bool get_service(const std::type_info &tinfo, std::shared_ptr<void> &ptr) override;
    virtual Log get_logger() const override;
    virtual std::string get_var(std::string_view var_name) const override;
    virtual std::span<const Account> get_accounts() const override;
    virtual std::span<const Instrument> get_instruments() const override;
    virtual const Config &get_config() const override;
    virtual void on_message(const MQBroker::Message &message, bool pm) noexcept override;
    virtual void subscribe_channel(std::string_view channel) override;
    virtual void unsubscribe_channel(std::string_view channel) override;
    virtual void send_message(std::string_view channel, std::string_view msg, IMQBroker::ConversationID cid) override;
    virtual void update_market(const Instrument &i, MarketEventType type, Function<void(AsyncResult<MarketEventData>)> &&cb) override;
    virtual Positions load_positions(std::string_view filter) const override;
    virtual Trades load_closed(Timestamp limit, std::string_view filter) const override;
    virtual std::string_view get_strategy_name() const override;
    virtual void series_erase_points(std::string_view series_name, uint64_t index_and_less) override;
    virtual uint64_t series_add_point(std::string_view series_name, std::string_view point_data) override;
    virtual ValueStream<std::string_view> load_series(std::string_view name) const override;
    virtual void receive_order_report(const Order &order, Function<void(AsyncResult<std::span<Fill> >)> &&cb) override;
    virtual void on_idle(Function<void(AsyncResult<void> )> &&fn) override;

    virtual quarkbot::VarSet<> get_vars(
            std::string_view prefix) const override;
    virtual quarkbot::VarSet<> get_vars(
            std::string_view start, std::string_view end) const override;
    virtual bool is_stop_requested() const override {return _stop_requested;}
    virtual void on_stop_requested(Function<void(AsyncResult<void>)> &&fn) override;
    virtual void on_market_event(Function<void(AsyncResult<MarketEvent>)> &&callback) override;
    virtual void on_orders_restored(Function<void(AsyncResult<std::span<Order> >)> &&callback) override;
    virtual void on_mq_message(Function<void(AsyncResult<Message>)> &&callback) override;

    virtual void stop() override;
protected:

    IControl &_control;
    std::unique_ptr<IStorage> _storage;
    std::unique_ptr<IStrategy> _strategy;
    Log _logger;
    MQBroker _mq;
    std::string _name;
    std::vector<Account> _accounts;
    std::vector<Instrument> _instruments;
    Config _config;
    bool _stop_requested = false;
    bool _stop_called = false;
    bool _in_scheduler = false;
    bool _orders_restored = false;
    Timestamp _event_time = Timestamp::min();

    struct MarketEventItem {
        Instrument i;
        MarketEventData event;
        bool operator==(const MarketEventItem &) const = default;
    };

    struct EvNoop {
        void operator()() {}
    };


    struct EvStart {
        BasicContext *me;
        void operator()();
    };

    struct EvRestoreOrders {
        BasicContext *me;
        std::vector<Order> orders;
        void operator()();
    };

    struct EvMarketEventItem{
        BasicContext *me;
        MarketEvent event;
        void operator()();
        bool operator==(const EvStart &) const ;
        std::size_t get_hash() const;
    };

    struct EvUpdateInstrument {
        BasicContext *me;
        Instrument i;
        AsyncResult<void> st;
        void operator()();
    };

    struct EvUpdateAccount {
        BasicContext *me;
        Account a;
        AsyncResult<void> st;
        void operator()();
    };

    struct EvUpdateMarket {
        BasicContext *me;
        Instrument i;
        MarketEventType type;
        AsyncResult<MarketEventData> ev;
        void operator()();
    };

    struct EvOrderReport {
        BasicContext *me;
        Order order;
        Order::Report report;
        void operator()();
    };

    struct EvStopRequest {
        BasicContext *me;
        void operator()();
    };

    struct EvMQ {
        BasicContext *me;
        MQBroker::Message msg;
        bool pm;
        void operator()();
    };

    struct EvCall {
        TimerEventCB _fn;
        void operator()();
    };

    using QueueItem = std::variant<
            EvNoop,
            EvStart,
            EvUpdateAccount,
            EvMarketEventItem,
            EvUpdateInstrument,
            EvOrderReport,
            EvMQ,
            EvRestoreOrders,
            EvUpdateMarket,
            EvStopRequest,
            EvCall
            >;

    struct QueueItemHasher {
        std::size_t operator()(const QueueItem &x) const;
    };
    struct QueueItemCompare {
        bool operator()(const QueueItem &a, const QueueItem &b) const;
    };

    struct Batches {
        std::vector<Order> _batch_place;
        std::vector<Order> _batch_cancel;
    };

    using InstSubPair = std::pair<Instrument, MarketEventType>;
    struct InstSubPairHasher {
        std::size_t operator()(const InstSubPair &p) const {
            return Instrument::Hasher()(p.first);
        }
    };


    template<typename X> using CallbackList = Strategy::CallbackList<X>;

    std::mutex _queue_mx;
    DispatcherCore<QueueItem, QueueItemHasher, QueueItemCompare> _queue;
    std::map<ExchangeInfo, Batches> _exchanges;
    std::unordered_map<Account, Signaller<void>, Account::Hasher> _update_account_cbs;
    std::unordered_map<Instrument, Signaller<void>,Instrument::Hasher> _update_instrument_cbs;
    std::unordered_map<InstSubPair, Signaller<MarketEventData>,InstSubPairHasher> _update_market_cbs;
    std::unordered_map<Order, Signaller<std::span<Fill> >,Order::Hasher> _order_report;
    CallbackList<void> _on_idle_cbs;
    Signaller<MarketEvent> _on_market_event_cbs;
    Signaller<std::span<Order> > _on_restored_orders_cbs;
    Signaller<Message> _on_mq_message;

    Function<void(AsyncResult<void>)> _on_stop_cb;

    void begin_transaction();
    void commit();
    void rollback();
    void notify_queue();

    virtual void on_scheduled(Timestamp tp) noexcept override;
    template<std::invocable<> Fn>
    void call_strategy(Fn &&strategy_fn);

    void post(QueueItem &&);
    void post_collapse(QueueItem &&);
    std::vector<std::pair<Order, Order::Report> > restore_orders();

    void stop_internal();
};


}
