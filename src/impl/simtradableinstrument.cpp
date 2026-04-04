#include "simtradableinstrument.hpp"
#include "siminstrument.hpp"
#include "ifc/memory.hpp"
#include <memory>
namespace quarkbot {

void SimTradableInstrument::report_fill(const Fill &fill) {
    _position = _position.trade(fill.side == Side::buy ? fill.amount : -fill.amount, fill.contract.multiplier * fill.price);
    report_price(fill.price);
}

void SimTradableInstrument::report_price(Decimal price) {
    _last_price = price;
    const auto &info = _instrument->get_info();
    _account->report_upnl(info.pnl_currency.id, info.calc_pnl(_position.getAverageCost(), price, _position.getOpenPosition()));
}

void SimTradableInstrument::report_margin(Decimal margin){
    _account->report_margin(_instrument->get_info().pnl_currency.id, margin);
}

void SimTradableInstrument::report_order_blocked(Decimal blocked) {     
    _account->report_order_blocked(_instrument->get_info().pnl_currency.id, blocked);
}

SimTradableInstrument::Info SimTradableInstrument::get_info() const {
        return _instrument->get_info();
    }

PExchange SimTradableInstrument::get_exchange() const {
        return _instrument->get_exchange();
    }
std::shared_ptr<IEventStreamBase> SimTradableInstrument::subscribe_stream_internal(std::string_view type, const StreamParams &params) {
        return _instrument->subscribe_stream_internal(type, params);
    }
awaitable<PTradableInstrument> SimTradableInstrument::create_tradable_instrument(PAccount account) {
        return _instrument->create_tradable_instrument(account);
    }
PAccount SimTradableInstrument::get_account() const {
        return _account;
    }
PMarketInstrument SimTradableInstrument::get_instrument() const {
        return _instrument;
    }

SimTradableInstrument::OrderState::OrderState(OrderParametersGen<Decimal> params, 
              std::shared_ptr<SimTradableInstrument> instrument,
              std::string name,
              std::weak_ptr<State> replaced_order,
              PExecutionWorker worker
            ):Order::State(std::move(params),std::move(instrument), std::move(name), std::move(replaced_order))
            ,_worker(std::move(worker)) {}

void SimTradableInstrument::OrderState::report_fill(const Order &ord, const Fill &fill) {
    OrderEx o(ord);
    auto st = o.get_state();
    auto instr = std::static_pointer_cast<SimTradableInstrument>(st->instrument);
    st->_worker->run(coro_report_fill(&mem_pool,std::move(instr), ord, fill));
}
void SimTradableInstrument::OrderState::report_status(const Order &ord, const OrderStatusUpdate &status) {
    OrderEx o(ord);
    auto st = o.get_state();
    auto instr = std::static_pointer_cast<SimTradableInstrument>(st->instrument);
    st->_worker->run(coro_report_status(&mem_pool,std::move(instr), status));

}
void SimTradableInstrument::OrderState::init(const Order &ord, const OrderInitialUpdate &init) {
    OrderEx o(ord);
    auto st = o.get_state();
    auto instr = std::static_pointer_cast<SimTradableInstrument>(st->instrument);
    st->_worker->run(coro_report_init(&mem_pool,std::move(instr), init));

}
void SimTradableInstrument::OrderState::report_blocked(const Order &ord, Decimal dec) {
    OrderEx o(ord);
    auto st = o.get_state();
    auto instr = std::static_pointer_cast<SimTradableInstrument>(st->instrument);
    st->_worker->run(coro_report_blocked(&mem_pool,std::move(instr), dec));

}

coro::coroutine<void, coro::pmr_allocator<>> SimTradableInstrument::coro_report_fill(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument, Order ord, Fill fill ) {
//todo: implmenet logic in coroutine
}
coro::coroutine<void, coro::pmr_allocator<>> SimTradableInstrument::coro_report_status(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument, OrderStatusUpdate update) {
//todo: implmenet logic in coroutine
}
coro::coroutine<void, coro::pmr_allocator<>> SimTradableInstrument::coro_report_init(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument, OrderInitialUpdate update) {
//todo: implmenet logic in coroutine
}
coro::coroutine<void, coro::pmr_allocator<>> SimTradableInstrument::coro_report_blocked(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument, Decimal dec) {
//todo: implmenet logic in coroutine
}

}


