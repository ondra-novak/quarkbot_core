#include "simtradableinstrument.hpp"
#include "ifc/account.hpp"
#include "ifc/execution_worker.hpp"
#include "ifc/order.hpp"
#include "ifc/types.hpp"
#include "siminstrument.hpp"
#include "simaccount.hpp"
#include "ifc/memory.hpp"
#include "utils/decimal.hpp"
#include <cassert>
#include <memory>
#include <stdexcept>
namespace quarkbot {

using WalletInfo = IAccount::WalletInfo;

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
    if (!_account->update_wallet(info.pnl_currency, [&](WalletInfo &w){w.unrealized_pnl = _position.get_upnl(price, info);})) {
        liquidation();
    }
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

coro::coroutine<void, coro::pmr_allocator<>> SimTradableInstrument::coro_report_fill(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument, Order ord, Fill fill ) {
    instrument->flush_orders_stats();
    instrument->report_fill(fill);
    ord.update_order(std::move(fill));
    co_return;
}
coro::coroutine<void, coro::pmr_allocator<>> SimTradableInstrument::coro_report_status(coro::pmr_allocator<>,std::shared_ptr<SimTradableInstrument> instrument, Order ord,OrderStatusUpdate update) {
    instrument->flush_orders_stats();
    ord.update_order(std::move(update));
    if (instrument->liquidation_order && ord == *instrument->liquidation_order && ord.done()) {
        instrument->liquidation_order.reset();
    }
    co_return;
}
coro::coroutine<void, coro::pmr_allocator<>> SimTradableInstrument::coro_report_init(coro::pmr_allocator<>, std::shared_ptr<SimTradableInstrument> instrument,Order ord, OrderInitialUpdate update) {
    instrument->flush_orders_stats();
    ord.update_order(std::move(update));
    co_return;
}

void SimTradableInstrument::flush_orders_stats() {
    if (_new_order_stats) {
        const auto &info = _instrument->get_info();
        bool leveraged = info.is_leveraged();
        bool has_wallet = info.asset_has_wallet();
        if (leveraged) {
            if (!_account->update_wallet(info.pnl_currency, [&](WalletInfo &w){
                Decimal v = _position.get_volume(info);
                Decimal b = _new_order_stats->buy_turnover;
                Decimal s = _new_order_stats->sell_turnover;
                if (_position.side == Side::buy) b+=v;
                else if (_position.side == Side::sell) s+=v;
                Decimal m = std::max(b,s)* reciprocal(info.leverage);
                w.initial_margin = m;
            },true)) {
                liquidation();
            }
        } else {
            _position_blocked = _new_order_stats->sell_quantity;
            _account->update_wallet(info.quote_currency, [&](WalletInfo &w){
                w.order_blocked = _new_order_stats->buy_turnover;
            });
            if (has_wallet) {
                _account->update_wallet(info.quote_currency, [&](WalletInfo &w){
                    w.order_blocked = _new_order_stats->sell_quantity;
                });
            } else{
                _position_blocked = _new_order_stats->sell_quantity;
            }
        }
        _new_order_stats.reset();
    }

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
    Decimal turnover = new_order.get_turnover(_last_price);

    const Info &info = _instrument->get_info();    


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
        }
    }

    auto insufficient_funds = [&]{
        new_order.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::insufficient_funds});
    };

    if (info.is_leveraged()) {
        _account->update_wallet(info.pnl_currency.id, [&](WalletInfo &w) {
            Decimal margin = turnover * reciprocal(info.leverage);
            if (w.remaining_balance() < margin ) insufficient_funds();
            else w.initial_margin += margin;
        });
    } else {
        if (params.side == Side::buy) {
            _account->update_wallet(info.quote_currency.id, [&](WalletInfo &w) {
                if (w.remaining_balance() < turnover) insufficient_funds();
                else w.order_blocked += turnover;
            });
        } else if (params.side == Side::sell) {
            if (info.asset_has_wallet()) {
                _account->update_wallet(info.asset_wallet->id, [&](WalletInfo &w){
                    if (w.remaining_balance() < params.quantity) insufficient_funds();
                    else w.order_blocked += params.quantity;
                });
            } else {
                if (_position.amount - _position_blocked < params.quantity) insufficient_funds();
                else _position_blocked += _position_blocked;
            }
        }
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
    st->_worker->run(coro_report_status(&mem_pool,shared_from_this(),ord,status));
}
void SimTradableInstrument::on_order_accept(const Order &ord, const OrderInitialUpdate &init) {
    OrderEx o(ord);
    auto st = o.get_state();
    st->_worker->run(coro_report_init(&mem_pool,shared_from_this(),ord, init));
}

void SimTradableInstrument::on_start_update_blocks() {
    _new_order_stats.emplace();
}

void SimTradableInstrument::on_update_blocks(Side side, Decimal quantity, Decimal price) {
    const Info &info = get_info();
    if (side == Side::buy) {
        _new_order_stats->buy_turnover += info.calc_turnover_pnl_currency(price,quantity);
        _new_order_stats->buy_quantity += quantity;
    } else if (side == Side::sell) {
        _new_order_stats->sell_turnover += info.calc_turnover_pnl_currency(price,quantity);
        _new_order_stats->sell_quantity += quantity;

    }
}


}




