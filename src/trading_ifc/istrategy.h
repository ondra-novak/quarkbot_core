#pragma once
#include "strategy_context.h"
#include "config_desc.h"
#include "orderbook.h"
#include <queue>


namespace trading_api {



class IStrategy {
public:

    using Message = IMQBroker::Message;

    static constexpr unsigned int signal_configuration_changed  = 0;

    virtual ~IStrategy() = default;

    virtual ConfigSchema get_config_schema() const = 0;


    ///called on initialization
    virtual void on_init(IContext *ctx) = 0;

    ///called when strategy is officially started, when context is fully initialized and ready to process requests
    /**
     * This function is called right after on_init and it is standard event (on_init is
     * not event).
     */
    virtual void on_start() = 0;

    ///called when market event happened
    /**
     * You need to subscribe to given market event. See Context::subscribe
     *
     * @param i instrument
     * @param tk ticker
     */
    virtual void on_market_event(const Instrument &i, const MarketEvent &event) = 0;

    ///called when order state is updated (market triggers) and also at the beginning for all open orders
    virtual void on_order(const Order &ord) = 0;

    ///called when fill is detected
    /**
     * @param ord associated order
     * @param fill recorded fill
     * @return strategy's custom label. This allows to filter fills later. This
     * is also critical to calculate pnl. It is recommended to use different label
     * for different currency
     *
     * @note the label can have a an internal structure.
     *      you can filter labels by a prefix. For example "usd_1234" means
     *      usd for currency and 1234 is custom identifier. Label don't need to be
     *      unique.
     */
    virtual std::string on_fill(const Order &ord, const Fill &fill) = 0;


    ///called when MQ message is received
    /**
     * To gain access to MQ, use IContext::get_mq_client()
     */
    virtual void on_mq_message(const Message &) = 0;
    ///called when unhandled exception is detected anywhere in the strategy code
    /**
     * This function is called even if the unhandled exception happened in
     * a coroutine.
     *
     * The strategy can process an exception somehow, or rethrow the exception out of
     * the function. If the exception is thrown out, it causes rollback of
     * all orders and writes to the storage (serves as rollback of all)
     *
     * If the strategy exits function normally, transactions are commited as usual
     *
     * Default implementation rethrows, which rollbacks all changes
     *
     */
    virtual void on_unhandled_exception() = 0;

    ///Called when there are no events
    /**
     * Called once after all dispatched events has been processed. You can run
     * a code, which can handle results of all events previously received
     * This code should not take a long processing because during its execution,
     * no market event can be processed (but they are enqueued to be processed as
     * soon as possible). You need to exit this function to resume normal operation
     *
     * @retval true idle cycle is done, no more idle calls will happen
     * @retval false need more idle cycles
     * @note any idle processing can impact performance especially during backtesting.
     * Don't return always false as all empty idle cycles must be simulated
     * If you need delay in strategy, use timer.
     */
    virtual bool on_context_idle() = 0;

    ///update account is complete
    virtual void on_update_complete(const Account &a, AsyncStatus status) = 0;
    ///update instrument is complete
    virtual void on_update_complete(const Instrument &i, AsyncStatus status) = 0;

protected:


    template<typename Strategy>
    class OrderResult { // @suppress("Miss copy constructor or assignment operator")
    public:
        OrderResult(Strategy *s, Order ord):_s(s), _ord(ord) {}
        operator Order() const {return _ord;}
        template<std::invocable<const Order &> CB>
        OrderResult &operator >>(CB &&cb) {
            _s->add_callback(_ord, std::forward<CB>(cb));
            return *this;
        }

    protected:
        Strategy *_s;
        Order _ord;
    };

    template<typename Strategy>
    class Subscription { // @suppress("Miss copy constructor or assignment operator")
    public:
        Subscription(Strategy *s, SubscriptionType type, const Instrument &i):_s(s),_t(type),_i(i) {}
        template<std::invocable<const Instrument &, const MarketEvent &> CB>
        void operator >>(CB &&cb) {
            _s->add_subscription(_t, _i, std::forward<CB>(cb));
        }
    protected:
        Strategy *_s;
        SubscriptionType _t;
        const Instrument &_i;
    };

    template<typename Strategy>
    class MQSubscription { // @suppress("Miss copy constructor or assignment operator")
    public:
        MQSubscription(Strategy *s, std::string_view c):_s(s),_c(c) {}
        template<std::invocable<const Message &> CB>
        void operator >>(CB &&cb) {
            _s->add_mq_subscription(_c, std::forward<CB>(cb));
        }
    protected:
        Strategy *_s;
        std::string_view _c;

    };
    template<typename Strategy>
    class IdleAwaiter { // @suppress("Miss copy constructor or assignment operator")
    public:
        IdleAwaiter(Strategy *s):_s(s) {}
        template<std::invocable<> CB>
        void operator >>(CB &&cb) {
            _s->register_idle(std::forward<CB>(cb));
        }
        completion_awaiter operator co_await() {
            return [this](auto &&cb) {
                (*this) >> [cb = std::move(cb)]{
                    cb(AsyncStatus::ok);
                };
            };
        }
    protected:
        Strategy *_s;
    };

    class TimerAwaiter { // @suppress("Miss copy constructor or assignment operator")
    public:
        TimerAwaiter(IContext *ctx, Timestamp at, TimerID id):_ctx(ctx),_at(at),_id(id) {}
        template<std::invocable<> CB>
        void operator>>(CB &&cb) {
            _ctx->set_timer(_at, std::forward<CB>(cb), _id);
        }
        completion_awaiter operator co_await() {
            return [&](auto fn) {
                _ctx->set_timer(_at, [fn = std::move(fn)]{
                    AsyncStatus st;
                      fn(st);
                },_id);
            };
        }


    protected:
        IContext *_ctx;
        Timestamp _at;
        TimerID _id;

    };

    template<typename Strategy, typename Object>
    class UpdateAwaiter { // @suppress("Miss copy constructor or assignment operator")
    public:
        UpdateAwaiter(Strategy *s,const Object &o):_s(s),_o(o) {}
        template<std::invocable<AsyncStatus> CB>
        void operator>>(CB &&cb) {
            _s->register_update(_o, std::forward<CB>(cb));

        }
        completion_awaiter operator co_await() {
            return [&](auto cb) {
                _s->register_update(_o, std::move(cb));
            };
        }


    protected:
        Strategy *_s;
        const Object &_o;

    };


};



}
