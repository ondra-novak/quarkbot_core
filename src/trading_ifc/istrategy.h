#pragma once
#include "strategy_context.h"
#include "config_desc.h"
#include "orderbook.h"
#include "awaiter.h"
#include <queue>


namespace trading_api {



class IStrategy {
public:

    using Message = IMQBroker::Message;
    using ActiveOrders = std::vector<Order>;

    static constexpr unsigned int signal_configuration_changed  = 0;

    virtual ~IStrategy() = default;

    virtual ConfigSchema get_config_schema() const = 0;


    ///called on initialization
    virtual void on_init(IContext *ctx) = 0;

    ///called before on_start and carries active orders
    /** these orders are associated with the strategy, and they are probably
     * created by previous run, or it is part of initial state.
     * @param active_orders list of active orders.
     * @note all orders has origin and state 'restored'. Their state will be updated by
     * function on_order_report() which immediately follows per order.
     * That function can also report fills received
     * in between time. You should not process state of the orders now, you just need
     * to use them to restore the strategy state.
     */
    virtual void on_active_orders(ActiveOrders active_orders) = 0;

    ///called when strategy is officially started, when context is fully initialized and ready to process requests
    /**
     * @note the function is called `before` on_start();
     *
     * - on_active_orders()
     * - on_start()
     * - on_order_report (each active order)
     * - on_order_report (each active order)
     * - on_order_report (each active order)
     * - on_order_report (each active order)
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

    ///called when order state is updated or when fill detected
    /**
     * @param ord order which status changed. You can read new status on the order.
     * @param fills all fills which are part of this report.
     *
     * @note the field fills don't need to contain all fills. More fills can
     * be received by next report. There is no specification how many fills
     * can reported in signle report. It is also possible to send a report without
     * fills, which means, that order status changed, but no fills has been made
     *
     */
    virtual void on_order_report(const Order &ord, std::vector<Fill> fills) = 0;

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
    virtual void on_update_complete(const Account &a, AsyncResult<void> result) = 0;
    ///update instrument is complete
    virtual void on_update_complete(const Instrument &i, AsyncResult<void> result) = 0;
    ///update instrument market state (ticker, orderbook etc)
    virtual void on_update_complete(const Instrument &i, MarketEventType type, AsyncResult<MarketEvent> result) = 0;



};



}
