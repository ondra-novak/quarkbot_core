#pragma once

#include <basic_coro/awaitable.hpp>
#include "ifc/defs.hpp"
#include "ifc/stream_defs.hpp"
#include "ifc/streaming.hpp"
#include "market_instrument.hpp"
#include "order.hpp"
#include "utils/acb.hpp"
#include "utils/function_view.hpp"
#include "storage.hpp"
#include <chrono>
namespace quarkbot {



class ITradableInstrument {
public:
    virtual ~ITradableInstrument() = default;

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

    ///Serializes order state
    /**
        @param ord order
        @return serialized form of the order, The content can be any binary data which enables function restore_order_state
        @note result can be store into database
    */
    virtual SerializedOrder serialize_order(Order ord) = 0;
    ///Restores order state
    /**
        @param ord serialized form of the order
        @return restored order. 
        @note restored order can have state OrderState::restured. Order's actual state is updated asynchronously once
        the order is found on the exchange in order history. If this operation fails, state is changed to lost.
    */
    virtual Order restore_order(SerializedOrder ord) = 0;

    ///Cancel order 
    /**
    This handles implementation of function cancel() on order
    */
    virtual void cancel_order(Order order) = 0;

    ///Cancel all orders associated with this instrument
    /**
        Cancels orders managed by this strategy. 
        Doesn't cancel any other orders
        @retval true canceled some orders
        @retval false no orders found to cancel
    */
    virtual bool cancel_all_orders() = 0;

    ///Get associated account
    virtual PAccount get_account() const = 0;

    ///Retrieves position (from exchange)
    virtual awaitable<Position> get_position() const = 0;

    ///converts tradable instrument into market instrument
    virtual PMarketInstrument get_instrument() const = 0;

    ///Internal
    virtual std::unique_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams *params) = 0;


    ///Subscribe account event stream
    template<StreamType<TradableInstrumentStreamTypeItem> T>
    EventStream<T> subscribe() {
        auto x =  subscribe_stream_internal(T::type, stream_params<T>);
        if (x) return EventStream<T>(std::move(x));
        else return EventStream<T>();
    }

    
    const IMarketInstrument::Info &get_info() const {
        return get_instrument()->get_info();
    }

    PExchange get_exchange() const {
        return get_instrument()->get_exchange();
    }

    OrderParameters convert_request_to_params(OrderRequest req, Side cur_position_side) {
        const auto &info = get_info();
        int aps = static_cast<int>(req.side);
        int aqs = req.side == cur_position_side?1:-1;
        return {
            req.side,
            req.type,
            req.quantity.get_rounded(info.lot_size_increment, aqs),
            req.limit_price.get_rounded(info.price_increment, aps),
            req.stop_price.get_rounded(info.price_increment, aps),
            req.leverage,
            req.reduce_only,
            req.hedge
        };
    }

};

///Streaming type - listen on fills from other sources - for example from other strategies, liquidation engine or user's manual trades.
struct ExternalFill : public Fill, public TradableInstrumentStreamTypeItem {
    static constexpr Type type = "external_fill";
    Fill &view() {return *this;}
};

///Streaming type - listen on funding events, if applicable for the instrument
struct FundingEvent : public TradableInstrumentStreamTypeItem {
    /// amount for this funding
    Decimal amount;
    /// rate,  if the funding is in different currency,
    double rate = 1.0;

    static constexpr Type type = "funding";
    FundingEvent &view() {return *this;}
};

///Streaming type - listen on order status updates
/**
This events are intended for storing data into storage
Stream is active when order state is changed or fill is detected

@note recommended use - store fill and state atomically.
 */
class OrderEvent: public TradableInstrumentStreamTypeItem {
public:
    ///contains order id (key type, must be unique) (depend on exchange)
    std::string order_id;
    ///contains serialized state of the order . If missing, order is done and need to be no longer stored 
    std::optional<std::string> serialized_state;
    ///contains fill, if detected - you should put order state and fill into single database transaction
    std::optional<Fill> fill;
};

///implementation of cancel_order for order
inline void Order::cancel() {
    _state->instrument->cancel_order(*this);
}

///implementation of get_turnover for order
/** Because Order definition doesn't see ITradableInstrument, the implementation is done here */
inline Decimal Order::get_turnover(Decimal price, Decimal filled) const {
        const auto &params = get_parameters();
        const auto &info = get_instrument()->get_info();
        filled = std::min(filled, params.quantity);
        auto leaves = params.quantity - filled;
        Decimal t1 = 0;
        Decimal t2 = 0;
        if (is_limit_order(params.type)) {
            t1 = info.calc_turnover_pnl_currency(params.limit_price, leaves);
        } 
        if (is_stop_order(params.type)) {
            t2 = info.calc_turnover_pnl_currency(params.stop_price, leaves);
        }
        if (t1 > t2) return t1;
        if (t2 > t1) return t2;
        if (t1) return t1;
        return info.calc_turnover_pnl_currency(price,leaves);
    }

}

