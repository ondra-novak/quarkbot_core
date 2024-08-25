#pragma once
#include <memory>

#include "../trading_ifc/instrument.h"
#include "../trading_ifc/account.h"
#include "../trading_ifc/orderbook.h"
#include "../trading_ifc/tickdata.h"
#include "../trading_ifc/order.h"
#include "../trading_ifc/fill.h"
#include "../trading_ifc/function.h"
#include "../trading_ifc/awaiter.h"
#include "../trading_ifc/market_event.h"


namespace trading_api {



class IEventTarget;


using PEventTarget = std::shared_ptr<IEventTarget>;
using WPEventTarget = std::weak_ptr<IEventTarget>;

///Represents strategy (with context) from service provider side
/**
 * Each strategy is represented by this interface. When service provider detects
 * a market event, it calls a function on_event defined on this interface
 */
class IEventTarget {
public:

    virtual ~IEventTarget () {}
    ///called when update of an instrument is finished
    /**
     * @param i instrument updated
     * @note called under exchange's lock. It is expected, that event is put
     * into execution queue. Don't call exchange object directly from the event. Do
     * not perform blocking operations in this event
     */
    virtual void on_update(Instrument i, AsyncResult<void> st) = 0;

    ///called when update on an account is finished
    /**
     * @param a account updated
     * @note called under exchange's lock. It is expected, that event is put
     * into execution queue. Don't call exchange object directly from the event. Do
     * not perform blocking operations in this event
     */
    virtual void on_update(Account a, AsyncResult<void> st) = 0;

    ///called when subscription update
    /**
     * @param i instrument
     * @param subscription_type type of subscription
     *
     * @note actual market data are not part of event. When event is processed
     * the strategy must read last market data from the exchange object
     */
    virtual void on_subscription_event(Instrument i, MarketEvent event) = 0;

    ///called when update_market is complete
    /**
     * @param i instrument
     * @param st operation status
     * @param subscription_type
     */
    virtual void on_update(Instrument i, MarketEventType type, AsyncResult<MarketEvent> ev) = 0;

    ///called when order state changed or fills
    virtual void on_order_report(Order order,Order::Report report) = 0;



};


}
