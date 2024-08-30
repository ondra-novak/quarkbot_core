#pragma once

#include "event_target.h"
#include "dispatcher.h"

#include <quarkbot/strategy.h>
#include "context_scheduler.h"
#include "storage.h"

#include "basic_exchange.h"
#include <deque>
#include <mutex>
#include <map>
#include <set>

namespace quarkbot {




/*scheduler object acts as invocable, which receives timestamp, function to call,
 * and pointer which serves as identification.
 *
 * If function is called, and there is already scheduled action, it
 * reschedules the action to new time point
 *
 */

template<typename Scheduler>
concept SchedulerType = (std::is_invocable_v<Scheduler, Timestamp, Function<void(Timestamp)>, const void *>);



class BasicContext: public IContext,
                    public IEventTarget,
                    public IMQBroker::IListener{
public:

    using GlobalScheduler = std::function<void(Timestamp,std::function<void(Timestamp)>, const void *)>;

    BasicContext(std::unique_ptr<IStorage> storage,
            GlobalScheduler gscheduler,
            Log logger,
            MQBroker mq,
            std::string_view strategy_name)
        :_scheduler(std::move(gscheduler))
        ,_storage(std::move(storage))
        ,_logger(std::move(logger), "{}", strategy_name)
        ,_mq(mq)
    {
    }

    BasicContext(const BasicContext &) = delete;
    BasicContext &operator=(const BasicContext &) = delete;

    ~BasicContext();


    void init(std::unique_ptr<IStrategy> strategy,
            std::vector<Account> accounts,
            std::vector<Instrument> instruments,
            Config config);

    virtual void on_update(Instrument i, AsyncResult<void> st) override;
    virtual void on_update(Account a, AsyncResult<void> st)  override;
    virtual void on_subscription_event(Instrument i, MarketEvent ev)  override;
    virtual void on_update(Instrument i, MarketEventType type, AsyncResult<MarketEvent> ev)  override;
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
    virtual void update_account(const Account &a) override;
    virtual void allocate(const Account &a, double equity) override;
    virtual bool clear_timer(TimerID id) override;
    virtual void update_instrument(const Instrument &i) override;
    virtual void unset_var(std::string_view var_name) override;
    virtual void set_var(std::string_view var_name, std::string_view value) override;
    virtual bool get_service(const std::type_info &tinfo, std::shared_ptr<void> &ptr) override;
    virtual Log get_logger() const override;
    virtual std::string get_var(std::string_view var_name) const override;
    virtual std::span<const Account> get_accounts() const override;
    virtual std::span<const Instrument> get_instruments() const override;
    virtual const Config &get_config() const override;
    virtual void on_message(MQClient::Message message) override;
    virtual void mq_subscribe_channel(std::string_view channel) override;
    virtual void mq_unsubscribe_channel(std::string_view channel) override;
    virtual void mq_send_message(std::string_view channel, std::string_view msg) override;
    virtual void update_market(const Instrument &i, MarketEventType type) override;

    virtual quarkbot::VarSet<> get_vars(
            std::string_view prefix) const override;
    virtual quarkbot::VarSet<> get_vars(
            std::string_view start, std::string_view end) const override;

protected:

    GlobalScheduler _scheduler;
    std::unique_ptr<IStorage> _storage;
    std::unique_ptr<IStrategy> _strategy;
    Log _logger;
    MQBroker _mq;
    std::vector<Account> _accounts;
    std::vector<Instrument> _instruments;
    Config _config;
    Timestamp _event_time = Timestamp::min();

    struct MarketEventItem {
        Instrument i;
        MarketEvent event;
        bool operator==(const MarketEventItem &) const = default;
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
        Instrument i;
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
        AsyncResult<MarketEvent> ev;
        void operator()();
    };

    struct EvOrderReport {
        BasicContext *me;
        Order order;
        Order::Report report;
        void operator()();
    };


    struct EvMQ {
        BasicContext *me;
        MQBroker::Message msg;
        void operator()();
    };

    using EvCall = Function<void()>;

    using QueueItem = std::variant<
            EvStart,
            EvUpdateAccount,
            EvMarketEventItem,
            EvUpdateInstrument,
            EvOrderReport,
            EvMQ,
            EvRestoreOrders,
            EvUpdateMarket,
            TimerEventCB
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


    std::mutex _queue_mx;
    DispatcherCore<QueueItem, QueueItemHasher, QueueItemCompare> _queue;

    std::map<ExchangeInfo, Batches> _exchanges;
    unsigned int _start_counter = 0;

    void begin_transaction();
    void commit();
    void rollback();
    void notify_queue();

    void on_scheduler(Timestamp tp) noexcept;
    template<std::invocable<> Fn>
    void call_strategy(Fn &&strategy_fn);

    void post(QueueItem &&);
    void post_collapse(QueueItem &&);
    std::vector<std::pair<Order, Order::Report> > restore_orders();
};


}
