#pragma once

#include "basic_coro/coroutine.hpp"
#include "basic_coro/pmr_allocator.hpp"
#include "ifc/defs.hpp"
#include "ifc/order.hpp"
#include "ifc/tradable_instrument.hpp"
#include "ifc/types.hpp"
#include "ifc/underlying.hpp"
#include "simexecutor.hpp"
#include "utils/decimal.hpp"

#include <memory>
#include <memory_resource>
#include <optional>


namespace quarkbot {

class SimInstrument;
class SimAccount;
class StrategyFragment;

class SimTradableInstrument final: public ITradableInstrument, public std::enable_shared_from_this<SimTradableInstrument> {
public:
    SimTradableInstrument(std::shared_ptr<SimInstrument> instr, std::shared_ptr<SimAccount> account)
        :_instrument(std::move(instr)), _account(std::move(account)) {}


    void report_fill(const Fill &fill);    
    void report_price(Decimal price) ;


    auto get_sim_instrument() const {
        return _instrument;
    }
     
    
    virtual std::unique_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams *params) override ;
    virtual PAccount get_account() const override ;
    virtual PMarketInstrument get_instrument() const override ;
    virtual Order place_order(const OrderRequest &params, Order order_to_replace, std::string_view name = {}) override;
    virtual Order place_order(const OrderRequest &params, std::string_view name = {}) override;
    virtual void cancel_order(Order order) override;
    virtual bool cancel_all_orders() override;
    virtual awaitable<Position> get_position() const override {return _position;}
    virtual SerializedOrder serialize_order(Order ord) override;
    virtual Order restore_order(SerializedOrder ord) override;


    void on_order_fill(const Order &ord, const Fill &fill);
    void on_order_status(const Order &ord, const OrderStatusUpdate &status);        
    void on_order_accept(const Order &ord, const OrderInitialUpdate &status);        

protected:
    std::shared_ptr<SimInstrument> _instrument;
    std::shared_ptr<SimAccount> _account;   

    Position _position;
    Decimal _position_blocked = {}; //for spot
    Decimal _upnl;

    Decimal _last_price = {};


    class OrderState: public Order::State {
    public:
        PExecutionWorker _worker;
        Decimal _turnover;

        OrderState(OrderParametersGen<Decimal> params, 
              std::shared_ptr<SimTradableInstrument> instrument,
              std::string name,
              std::weak_ptr<State> replaced_order,
              PExecutionWorker worker
            );


    };


    class OrderEx: public Order {
    public:
        OrderEx(Order ord):Order(std::move(ord)) {}

        std::shared_ptr<OrderState> get_state() const {
            return std::static_pointer_cast<OrderState>(_state);
        }
    };

    bool add_order_blocking(OrderEx ord);
    void remove_order_blocing(OrderEx ord);

    static StrategyFragment coro_report_fill(std::shared_ptr<SimTradableInstrument> instrument, Order ord, Fill fill );
    static StrategyFragment coro_report_status(std::shared_ptr<SimTradableInstrument> instrument,Order ord, OrderStatusUpdate update);
    static StrategyFragment coro_report_init(std::shared_ptr<SimTradableInstrument> instrument,Order ord, OrderInitialUpdate update);

    void liquidation();
    std::optional<Order> liquidation_order; 


    virtual Order place_order(const OrderRequest &params, std::shared_ptr<OrderState> old_state, std::string_view name = {});
    

};

}
