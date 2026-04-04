#pragma once

#include <basic_coro/awaitable.hpp>
#include "ifc/defs.hpp"
#include "ifc/streaming.hpp"
#include "market_instrument.hpp"
#include "order.hpp"
#include "utils/acb.hpp"
#include "utils/function_view.hpp"
#include "storage.hpp"
#include <chrono>
namespace quarkbot {



class ITradableInstrument : public IMarketInstrument{
public:
    virtual ~ITradableInstrument() = default;

    struct RiskLimits {
        ///allocated equity for this instrument (in counter underlying)
        double allocated_equity = 0;
        ///max allowed leverage
        double max_leverage = 0;
        ///max allowed position (short/long)
        double max_position = 0;
        ///allowed trading size (buy - long, sell - short, otherwise unlimited)
        Side allowed_side = Side::undetermined;
    };


    using Position = ACBCalculator<Decimal>;

    ///Place an order on the instrument
    /**
    @param params order parameters
    @param order_to_replace (optional) reference to existing order, which will be replaced. The way how
    order is replaced depends on exchange. Replaced order is finished with replaced status. 
    To prevent double execution in case of failure, the original order can be cancaled 
    even if replace fails.
    @param name order name, any arbitrary text which helps to strategy to identify the order
     */
    virtual Order place_order(const OrderRequest &params, Order order_to_replace, std::string_view name = {}) = 0;
    virtual Order place_order(const OrderRequest &params, std::string_view name = {}) = 0;

    ///Cancel order 
    /**
    This handles implementation of function cancel() on order
    */
    virtual void cancel_order(Order order) = 0;

    ///Attach storage/database and restore stored orders
    /**
        The storage is used to store any created or canceled orders automatically and also record all fills.
        On restart, all stored orders are restored by calling the provided callback for each restored order.
        The orders are in "restored" state until any update is posted to them.

        The strategy should assign a storage to each tradable instrument it uses before placing any orders 
        - best during initialization.

        If no storage is attached, no orders are stored and no orders are restored on restart. Fills can be stored in memory temporarily
        during the lifetime of the instrument, but they are not persisted.

        @param storage associated strategy storage
        @param callback function called for every restored order. Restored orders are in "restored" state until. 
        Any updates happened before order's restoration should be post to the strategy as an order event

     */
    virtual void attach_storage(PStorage storage, function_view<void(Order)> callback) = 0;
    ///Get associated account
    virtual PAccount get_account() const = 0;

    ///Retrieves last know position
    /**
        @return current position on the instrument
        @note on spot market, it returns exactly same value as query to wallet with underlying asset. On futures or derivates,
        it returns count of held contracts.

        @note function is asynchronous. It is much faster to count position from fills.
    */
    virtual coro::awaitable<Position> get_position() const = 0;

    virtual RiskLimits get_limits() const = 0;

    ///converts tradable instrument into market instrument
    virtual PMarketInstrument get_instrument() const = 0;

};

inline void Order::cancel() {
    _state->instrument->cancel_order(*this);
}


}