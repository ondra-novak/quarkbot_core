#pragma once

#include "basic_coro/coroutine.hpp"
#include "basic_coro/pmr_allocator.hpp"
#include "ifc/defs.hpp"
#include "ifc/order.hpp"
#include "ifc/tradable_instrument.hpp"
#include "simexecutor.hpp"
#include "utils/decimal.hpp"

#include <memory>
#include <memory_resource>
#include <optional>


namespace quarkbot {

class SimInstrument;
class SimAccount;

class SimTradableInstrument: public ITradableInstrument {
public:
    SimTradableInstrument(std::shared_ptr<SimInstrument> instr, std::shared_ptr<SimAccount> account)
        :_instrument(std::move(instr)), _account(std::move(account)) {}


    void report_fill(const Fill &fill);    
    void report_price(Decimal price) ;
    void report_margin(Decimal margin);
    void report_order_blocked(Decimal blocked);


    auto get_sim_instrument() const {
        return _instrument;
    }
     
    virtual Info get_info() const override ;
    virtual PExchange get_exchange() const override ;
    virtual std::shared_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams &params) override ;
    virtual awaitable<PTradableInstrument> create_tradable_instrument(PAccount account) override ;
    virtual PAccount get_account() const override ;
    virtual PMarketInstrument get_instrument() const override ;
    virtual Order place_order(const OrderRequest &params, Order order_to_replace, std::string_view name = {}) override;
    virtual Order place_order(const OrderRequest &params, std::string_view name = {}) override;
    virtual void cancel_order(Order order) override;
    virtual void attach_storage(PStorage , function_view<void(Order)>) override {/* not implemented */}
    virtual coro::awaitable<Position> get_position() const override {return _position;}
    virtual RiskLimits get_limits() const override {return _risk_limits;}


protected:
    std::shared_ptr<SimInstrument> _instrument;
    std::shared_ptr<SimAccount> _account;   
    Position _position;
    Decimal _last_price = {};
    RiskLimits _risk_limits = {};


    class OrderState: public Order::State, public SimExecutor::IExecutionResult {
    public:
        OrderState(OrderParametersGen<Decimal> params, 
              std::shared_ptr<SimTradableInstrument> instrument,
              std::string name,
              std::weak_ptr<State> replaced_order,
              PExecutionWorker worker
            );

        virtual void report_fill(const Order &ord, const Fill &fill) override;
        virtual void report_status(const Order &ord, const OrderStatusUpdate &status) override;
        virtual void init(const Order &ord, const OrderInitialUpdate &init) override;
        virtual void report_blocked(const Order &ord,Decimal dec) override;

        virtual ~OrderState() = default;


    protected:
        PExecutionWorker _worker;

    };


    class OrderEx: public Order {
    public:
        OrderEx(Order ord):Order(std::move(ord)) {}

        std::shared_ptr<OrderState> get_state() const {
            return std::static_pointer_cast<OrderState>(_state);
        }
    };

    static coro::coroutine<void, coro::pmr_allocator<> > coro_report_fill(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument, Order ord, Fill fill );
    static coro::coroutine<void, coro::pmr_allocator<>> coro_report_status(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument, OrderStatusUpdate update);
    static coro::coroutine<void, coro::pmr_allocator<>> coro_report_init(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument, OrderInitialUpdate update);
    static coro::coroutine<void, coro::pmr_allocator<>> coro_report_blocked(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument, Decimal dec);

};

}
