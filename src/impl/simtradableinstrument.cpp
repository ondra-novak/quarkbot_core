#include "simtradableinstrument.hpp"
#include "ifc/account.hpp"
#include "ifc/execution_worker.hpp"
#include "ifc/market_events.hpp"
#include "ifc/order.hpp"
#include "ifc/streaming.hpp"
#include "ifc/types.hpp"
#include "siminstrument.hpp"
#include "simaccount.hpp"
#include "ifc/memory.hpp"
#include "utils/decimal.hpp"
#include <cassert>
#include <memory>
#include <stdexcept>
namespace quarkbot {

using WalletInfo = SimAccount::WalletInfoExt;

void SimTradableInstrument::report_fill(const Fill &fill) {
    const auto &info = _instrument->get_info();
    Decimal pnl =  _position.update(fill, info);
    if (info.is_leveraged()) {
        if (!_account->update_wallet(info.pnl_currency, [&](WalletInfo &w){w.balance += pnl;}, true)) {
            liquidation();
        }
    } else {
        Decimal cash = -static_cast<int>(fill.side) * info.calc_turnover_quote_currency(fill.price, fill.amount);                                
        Decimal asset = static_cast<int>(fill.side) * fill.amount;
        Decimal fees = fill.fees * fill.fee_rate;
        _account->update_wallet(info.quote_currency, [&](WalletInfo &w){w.balance += cash - fees;}, true);
        if (info.asset_has_wallet()) {
            _account->update_wallet(*info.asset_wallet, [&](WalletInfo &w){w.balance += asset;}, true);
        }
    }
    report_price(fill.price);
}

void SimTradableInstrument::report_price(Decimal price) {
    _last_price = price;
    const auto &info = _instrument->get_info();
    Decimal upnl = _position.get_upnl(price, info);
    if (!_account->update_wallet(info.pnl_currency, [&](WalletInfo &w){w.unrealized_pnl += upnl-_upnl ;},false)) {
        liquidation();
    }
    _upnl = upnl;
}


std::unique_ptr<IEventStreamBase> SimTradableInstrument::subscribe_stream_internal(std::string_view , const StreamParams *) {
        return nullptr;
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

coro::coroutine<void, coro::pmr_allocator<>> SimTradableInstrument::coro_report_fill(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument, Order ord, Fill fill ) {
    instrument->report_fill(fill);
    ord.update_order(std::move(fill));
    co_return;
}
coro::coroutine<void, coro::pmr_allocator<>> SimTradableInstrument::coro_report_status(coro::pmr_allocator<>,std::shared_ptr<SimTradableInstrument> instrument, Order ord,OrderStatusUpdate update) {
    ord.update_order(std::move(update));
    if (instrument->liquidation_order && ord == *instrument->liquidation_order && ord.done()) {
        instrument->liquidation_order.reset();
    }
    co_return;
}
coro::coroutine<void, coro::pmr_allocator<>> SimTradableInstrument::coro_report_init(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument,Order ord, OrderInitialUpdate update) {
    ord.update_order(std::move(update));
    co_return;
}


bool SimTradableInstrument::cancel_all_orders() {
    return _instrument->get_sim_exchange()->cancel_all_orders(shared_from_this());
}

void SimTradableInstrument::liquidation() {
    if (!cancel_all_orders()) {
        if (_position.amount > 0 && !liquidation_order) {
            liquidation_order.emplace(place_order({
                reverse(_position.side),
                OrderType::market,
                _position.amount,
                {},
                {},
                {},
                false,
                false,
                ExecutionReason::liquidation
            }));
        }
    }
}

Order SimTradableInstrument::place_order(const OrderRequest &req, std::shared_ptr<OrderState> old_state, std::string_view name) {

    auto worker = IExecutionWorker::current();
    if (!worker) throw std::runtime_error("Orders can be placed from execution thread only");
    auto st = std::make_shared<SimTradableInstrument::OrderState>(
        convert_request_to_params(req, _position.side),
        shared_from_this(),
        std::string(name),
        old_state,
        worker
    );

    Order new_order(st);    

    const auto &info = _instrument->get_info();    


    const auto &params = new_order.get_parameters();
    if (params.side != Side::buy && params.side != Side::sell) {
        new_order.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::invalid_params,"Invalid side"});
        return new_order;
    }
    if (params.quantity < info.min_lot_size) {
        new_order.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::too_small});
        return new_order;
    }
    if (is_limit_order(params.type) || is_stop_order(params.type)) {
        if (is_limit_order(params.type)) {
            if (params.limit_price <= 0) {
                new_order.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::invalid_params, "Missing limit price"});
                return new_order;
            }
            if (info.min_volume > info.calc_turnover_pnl_currency(params.limit_price, params.quantity)) {
                new_order.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::too_small});
                return new_order;
            }
            st->_turnover = info.calc_turnover_pnl_currency(params.limit_price, params.quantity);
        }
        if (is_stop_order(params.type)) {
            if (params.stop_price <= 0) {
                new_order.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::invalid_params, "Missing limit price"});
                return new_order;
            }
            if (info.min_volume > info.calc_turnover_pnl_currency(params.stop_price, params.quantity)) {
                new_order.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::too_small});
                return new_order;
            }
            st->_turnover = std::max(st->_turnover, info.calc_turnover_pnl_currency(params.stop_price, params.quantity));
        }
    } else {
        st->_turnover = info.calc_turnover_pnl_currency(_last_price, params.quantity);
    }

    if (!add_order_blocking(new_order)) {
        new_order.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::insufficient_funds});
        return new_order;
    }

    _instrument->get_sim_exchange()->place_order(new_order);    
    return new_order;
            
}

 
SerializedOrder SimTradableInstrument::serialize_order(Order ) {
    return "//todo";
}
Order SimTradableInstrument::restore_order(SerializedOrder ) {
    auto worker = IExecutionWorker::current();
    if (!worker) throw std::runtime_error("Orders can be placed from execution thread only");

    Order out(std::make_shared<OrderState>(OrderParameters{}, shared_from_this(),std::string("//todo"),  std::weak_ptr<OrderState>(), worker));
    out.update_order(OrderStatusUpdate{OrderStatus::lost});
    return out;
}


Order SimTradableInstrument::place_order(const OrderRequest &params, Order order_to_replace, std::string_view name){
    OrderEx rep_ord(order_to_replace);
    auto st = rep_ord.get_state();
    return place_order(params, st, name);
}
Order SimTradableInstrument::place_order(const OrderRequest &params, std::string_view name){
    return place_order(params,{}, name);
}
void SimTradableInstrument::cancel_order(Order order){
    _instrument->get_sim_exchange()->cancel_order(order);
}

void SimTradableInstrument::on_order_fill(const Order &ord, const Fill &fill) {
    OrderEx o(ord);
    auto st = o.get_state();
    st->_worker->run(coro_report_fill(&mem_pool,shared_from_this(), ord, fill));
}

void SimTradableInstrument::on_order_status(const Order &ord, const OrderStatusUpdate &status){
    OrderEx o(ord);    
    auto st = o.get_state();
    if (is_done_status(status.status)) remove_order_blocing(o);
    st->_worker->run(coro_report_status(&mem_pool,shared_from_this(),ord,status));
}
void SimTradableInstrument::on_order_accept(const Order &ord, const OrderInitialUpdate &init) {
    OrderEx o(ord);
    auto st = o.get_state();
    st->_worker->run(coro_report_init(&mem_pool,shared_from_this(),ord, init));
}


bool SimTradableInstrument::add_order_blocking(OrderEx ord) {
    auto st = ord.get_state();
    const auto &info = get_info();
    auto prev_st = std::static_pointer_cast<OrderState>(st->replaced_order.lock());
    Decimal sub_to = prev_st?prev_st->_turnover:0;
    Decimal to = st->_turnover - sub_to;
    bool ok = true;

    if (info.is_leveraged()) {
        if (!_account->update_wallet(info.pnl_currency, [&](WalletInfo &w){
            Decimal m = to * reciprocal(info.leverage);
            Decimal b = w.margin_buys;
            Decimal s = w.margin_sells;
            if (st->parameters.side == Side::buy) b += m;
            else if (st->parameters.side == Side::sell) s += m;            
            Decimal im =  w.maintenance_margin + std::max(
                std::max(b- (_position.side == Side::buy?b-_position.get_volume(info):0_dec),0_dec),
                std::max(s- (_position.side == Side::sell?s-_position.get_volume(info):0_dec),0_dec));            
            if (w.balance + w.unrealized_pnl < im) ok = false;
            else {
                w.initial_margin = im;
                w.margin_buys = b;
                w.margin_sells =s;
            }
        },false)) return false;
    } else {
        if (st->parameters.side == Side::buy) {
            if (_account->update_wallet(info.quote_currency, [&](WalletInfo &w){
                if (w.balance + w.unrealized_pnl + w.order_blocked < to) ok = false;
                w.order_blocked += to;
            },false)) return false;
        }
        if (info.asset_has_wallet()) {
            if (_account->update_wallet(*info.asset_wallet, [&](WalletInfo &w){
                if (w.balance + w.unrealized_pnl + w.order_blocked < st->parameters.quantity) ok = false;
                w.order_blocked += st->parameters.quantity;
            },false)) return false;
        } else {
            if (_position.amount - _position_blocked < st->parameters.quantity) ok = false;
            else _position_blocked += st->parameters.quantity;
        }
    }
    return ok;

}
void SimTradableInstrument::remove_order_blocing(OrderEx ord) {
    auto st = ord.get_state();
    const auto &info = get_info();
    auto prev_st = std::static_pointer_cast<OrderState>(st->replaced_order.lock());
    //note called before status applied, so check current status to determine whether order has been alredy opened - we know, that new status is done
    Decimal sub_to = prev_st && st->status == OrderStatus::sent?prev_st->_turnover:0;
    Decimal to = st->_turnover - sub_to;

    if (info.is_leveraged()) {
        _account->update_wallet(info.pnl_currency, [&](WalletInfo &w){
            Decimal m = to * reciprocal(info.leverage);
            Decimal b = w.margin_buys;
            Decimal s = w.margin_sells;
            if (st->parameters.side == Side::buy) b -= m;
            else if (st->parameters.side == Side::sell) s -= m;            
            Decimal im =  w.maintenance_margin + std::max(
                std::max(b- (_position.side == Side::buy?b-_position.get_volume(info):0_dec),0_dec),
                std::max(s- (_position.side == Side::sell?s-_position.get_volume(info):0_dec),0_dec));            
            w.initial_margin = im;
            w.margin_buys = b;
            w.margin_sells =s;
        },false);
    } else {
        if (st->parameters.side == Side::buy) {
            _account->update_wallet(info.quote_currency, [&](WalletInfo &w){
                w.order_blocked -= to;
            },false);
        }
        if (info.asset_has_wallet()) {
            _account->update_wallet(*info.asset_wallet, [&](WalletInfo &w){
                w.order_blocked -= st->parameters.quantity;
            },false);
        } else {            
             _position_blocked -= st->parameters.quantity;
        }
    }    
}

}




