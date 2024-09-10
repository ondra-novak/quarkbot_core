#pragma once
#include "strategy_context.h"
#include "config_desc.h"
#include "awaiter.h"
#include <queue>


namespace quarkbot {



class IStrategy {
public:

    using Message = IMQBroker::Message;
    using ActiveOrders = std::vector<Order>;


    static constexpr unsigned int signal_configuration_changed  = 0;

    virtual ~IStrategy() = default;

    virtual ConfigSchema get_config_schema() const = 0;


    ///called on initialization
    virtual void on_init(IContext *ctx) = 0;


    ///called when strategy is officially started, when context is fully initialized and ready to process requests
    virtual void on_start() = 0;


    ///called when market event happened
    /**
     * You need to subscribe to given market event. See Context::subscribe
     *
     * @param i instrument
     * @param event market event
     */
    virtual void on_market_event(const MarketEvent &event) = 0;

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
    virtual void on_order_report(Order ord, std::span<Fill> fills) = 0;

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



};



}
