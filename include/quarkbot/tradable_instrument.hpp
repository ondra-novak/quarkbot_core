#pragma once

#include "market_instrument.hpp"
#include "abstract/itradable_instrument.hpp"
#include "hash/class_hash.hpp"
#include "account.hpp"
#include "order.hpp"
#include <concepts>
#include <cstddef>

namespace quarkbot {

template<typename T>
concept TradableInstrumentStream = requires {
    typename T::TradableInstrumentStream;
};


class Exchange;

class TradableInstrument: public Wrapper<ITradableInstrument> {
public:


    using Wrapper<ITradableInstrument>::Wrapper;
    ///Place order on this instrument
    /**
    @param params order parameters
    @param name optional name for order, can be used for debugging or logging purposes. 
    @return Order object representing placed order. You can co_await on this object for order updates or read fills from it.
     */   
    template<std::derived_from<OrderRequest> _Req = OrderRequest>
    Order place_order(const _Req &params){return Order(
        _ptr->place_order(params, {},  class_hash<_Req>));
    }

    ///Replace order on this instrument
    /**
    @param params new order parameters
    @param order_to_replace order to replace. It must be active order on this instrument.   
    @param name optional name for order, can be used for debugging or logging purposes.
    @return Order object representing placed order. You can co_await on this object for order updates or read fills from it.
     */     
    template<std::derived_from<OrderRequest> _Req = OrderRequest>
    Order place_order(const _Req &params, Order order_to_replace) {
        return _ptr->place_order(params, order_to_replace.get_handle(),class_hash<_Req>);
    }
    ///Attach storage to this instrument and restore opened orders from storage
    /**
    The storage is used to remember opened order, to store fills and other statistics for later retrieval.
    You should attach storage per instrument at the very beginning of the strategy.
    @param storage storage to attach
    @param key_name name of the key in storage, where orders will be stored. It shoud be unique per instrument.
    @return vector of active orders. Each order receives status "restored". The actual status and fills are later retrieved throug
    the co_awaiting on each order. 

    The main operation of this function is to find and identify orders on the exchange and retrieve its state and history.
    If the order is not found onf the exachnge, it receives status "lost".    
    */
    std::vector<Order> attach_storage(PStorage storage, std::string key_name) {
        return _ptr->attach_storage(std::move(storage), std::move(key_name));
    }
    ///Cancel alll orders on this instrument
    bool cancel_all_orders() {
        return _ptr->cancel_all_orders();
    }

    ///Get current position on this instrument
    /**
    It returns actual position on the exchange (if available). For the strategy logic, it is recommended to track position by processing fills.
    This function performs a request to the exchange which can take some time, this is the reason why the function is asynchronous.
    If position is not available, async operation is marked canceled.
     */
    awaitable<Position> get_position() const {return _ptr->get_position();}

    ///Get account associated with this instrument
    Account get_account() const{return _ptr->get_account();   }

    ///Get information about this instrument
    const IMarketInstrument::Info &get_info() const {
        return _ptr->get_instrument()->get_info();
    }
    ///get exchange associated with this instrument
    Exchange get_exchange() const;

    ///Convert to market instrument
    operator MarketInstrument() const {
        return MarketInstrument(_ptr->get_instrument());
    }

    ///Convert to market instrument
    MarketInstrument as_market_instrument() const {
        return MarketInstrument(_ptr->get_instrument());
    }



    ///Convert order request to order parameters.
    /**
    Conversion adjusts quantity and price to the increments of the instrument, 
    also can adjust other parameters based on request and current position. 
    You can override this function in your implementation of ITradableInstrument 
    to provide custom conversion logic.
    */
    OrderParameters convert_request_to_params(const OrderRequest &req, Side cur_position_side) {
        return _ptr->convert_request_to_params(req, cur_position_side);
    }

   

    ///Subscribe to stream of events related to this instrument
    /**
    You can subscribe only streams implementing TradableInstrumentStreamTypeItem interface.
    For example ExternalFill or FundingUpdate. These streams are private - related to the instrument and account.
    If you need to subscribe public streams (market streams), you can subscribe to them through MarketInstrument.    
    */
    template<TradableInstrumentStream T>
    requires(StreamWithoutParam<T> || StreamWithConstantParam<T>)
    EventStream<T> subscribe() {
        return _ptr->subscribe<T>();
    }

    template<TradableInstrumentStream T>
    requires(StreamWithParam<T>)
    EventStream<T> subscribe(typename T::Param params) {
        return _ptr->subscribe<T>(params);
    }

    ///Subscribe market data
    template<MarketInstrumentStream T>
    requires(StreamWithoutParam<T> || StreamWithConstantParam<T>)
    EventStream<T> subscribe() {
        return _ptr->get_instrument()->subscribe<T>();
    }

    ///Subscribe market data
    template<MarketInstrumentStream T>
    requires(StreamWithParam<T>)
    EventStream<T> subscribe(typename T::Param params) {
        return _ptr->get_instrument()->subscribe<T>(params);
    }


};



inline TradableInstrument Order::get_instrument() const{
    return TradableInstrument(_ptr->get_instrument());
}


inline TradableInstrument MarketInstrument::create_tradable_instrument(const Account &acc) const{
    return _ptr->create_tradable_instrument(acc.get_handle());
}

///implementation of get_turnover for order
/** Because Order definition doesn't see ITradableInstrument, the implementation is done here */
inline Decimal calc_turnover(const OrderParameters params, const TradableInstrument &instr,  Decimal price, Decimal filled)  {
        const auto &info = instr.get_info();
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

