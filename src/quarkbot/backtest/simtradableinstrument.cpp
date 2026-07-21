#include "simtradableinstrument.hpp"
#include "../common/orderdata.hpp"
#include "quarkbot/defs.hpp"
#include "quarkbot/selector.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/selector.hpp"
#include "simaccount.hpp"
#include "simexchange.hpp"
#include "siminstrument.hpp"        // IWYU pragma: keep - failed to detect by clangd
#include "quarkbot/storage_srl.hpp" // IWYU pragma: keep - template definitions
#include <cassert>
#include <memory>
#include <optional>
#include <stdexcept>
#include <variant>
namespace quarkbot {


void SimTradableInstrument::report_fill(const Fill &fill) {
    const auto &info = _instrument->get_info();
    Decimal pnl =  _position.update(fill, info);
    if (info.is_leveraged()) {
        if (!get_sim_account()->update_wallet(info.pnl_currency, [&](WalletInfo &w){w.balance += pnl;}, true)) {
            liquidation();
        }
    } else {
        Decimal cash = -static_cast<int>(fill.side) * info.calc_turnover_quote_currency(fill.price, fill.quantity);                                
        Decimal asset = static_cast<int>(fill.side) * fill.quantity;
//        Decimal fees = fill.fees * fill.fee_rate;
        get_sim_account()->update_wallet(info.quote_currency, [&](WalletInfo &w){w.balance += cash ;}, true);
        if (info.asset_has_wallet()) {
            get_sim_account()->update_wallet(*info.asset_wallet, [&](WalletInfo &w){w.balance += asset;}, true);
        }
    }
    report_price(fill.price);
}

void SimTradableInstrument::report_price(Decimal price) {
    _last_price = price;
    const auto &info = _instrument->get_info();
    Decimal upnl = _position.get_upnl(price, info);
    if (!get_sim_account()->update_wallet(info.pnl_currency, [&](WalletInfo &w){w.unrealized_pnl += upnl-_upnl ;},false)) {
        liquidation();
    }
    _upnl = upnl;
}


std::shared_ptr<IEventStreamBase> SimTradableInstrument::subscribe_stream(std::size_t hash, const void *) {
     if (hash == class_hash<ExternalFill>) {
        auto s = _liquidation_stream.lock();
        if (s == nullptr) {
            s = std::make_shared<QueueEventPublisher<ExternalFill> >();
            _liquidation_stream = s;
        }
        return s->create_subscriber();
    }
    return nullptr;
}


bool SimTradableInstrument::cancel_all_orders() {
    return get_sim_instrument()->get_sim_exchange()->cancel_all_orders(shared_from_this());
}

void SimTradableInstrument::liquidation() {
    if (!cancel_all_orders()) {
        if (_position.amount > 0 && !liquidation_order) {
            liquidation_order = place_order({
                "sim:liquidation",
                reverse(_position.side),
                OrderType::market,
                _position.amount,
                {},
                {},
                TimeInForce::gtc,
                {},
                false,
                false,
                false,
                true,
                ExecutionReason::liquidation
            },{},0);
        }
    }
}

struct CancelCallback {
    std::shared_ptr<SimExchange> exchange;
    void operator()(IOrder *ord) {
        exchange->cancel_order(ord);
    }
};



void SimTradableInstrument::finish_order(POrderData ord) {
        if (liquidation_order && ord == liquidation_order) {
            liquidation_order.reset();
        }        
        auto iter = std::find_if(_active_orders.begin(), _active_orders.end(), [&](const RegOrder &r){
            return r.order == ord;
        });
        if (iter != _active_orders.end()) {
            _active_orders.erase(iter);
        }
        update_margin();
}

void SimTradableInstrument::on_order_update(POrder ord_raw, OrderStatusUpdate &&status) {
   
    POrderData ord = std::static_pointer_cast<OrderInternalData>(ord_raw);

    selector(std::move(status), 
        [&](Fill &&f){
            report_fill(f);
            update_order(ord, std::move(f));
        },
        [&](OrderStatus &&st) {
            if (is_done_status(st)) {
                finish_order(ord);
            }
            update_order(ord, st);
        },
        [&](OrderRejectionReason &&st) {
            finish_order(ord);
            update_order(ord, st);
        },
        [&](OrderRejectionWithText &&st) {
            finish_order(ord);
            update_order(ord, std::move(st));
        },
        [&](auto &&st) {
            update_order(ord, std::move(st));
        }
    );
}

std::vector<Order> SimTradableInstrument::attach_storage(PStorage ,std::string ) {  
    return {};

}

POrderData SimTradableInstrument::create_order(const OrderParameters &params, POrder replaced_order, std::size_t ) {
    ExecutionWorker worker = ExecutionWorker::current();    
    auto st = std::make_shared<OrderWithCancelCallback<CancelCallback> >(
        params,
        shared_from_this(),
        replaced_order,
        worker.required().now(),
        CancelCallback{get_sim_instrument()->get_sim_exchange()}
    );
    return st;
}
void SimTradableInstrument::submit_order(POrderData order) {

    const auto &info = _instrument->get_info();    


    const auto &params = order->get_parameters();
    if (params.side != Side::buy && params.side != Side::sell) {
        return update_order(order,OrderRejectionWithText{ OrderRejectionReason::invalid_params,"Invalid side"});
    }
    if (params.quantity < info.min_quantity) {
        return update_order(order,OrderRejectionReason::too_small);
    }
    if (is_limit_order(params.type) || is_stop_order(params.type)) {
        if (is_limit_order(params.type)) {
            if (params.limit_price <= 0) {
                return update_order(order,OrderRejectionWithText{ OrderRejectionReason::invalid_params, "Missing limit price"});
            }
            if (info.min_turnover > info.calc_turnover_pnl_currency(params.limit_price, params.quantity)) {
                return update_order(order, OrderRejectionReason::too_small);
            }
        }
        if (is_stop_order(params.type)) {
            if (params.stop_price <= 0) {
                return update_order(order,{ OrderRejectionReason::invalid_params, "Missing limit price"});                
            }
            if (info.min_turnover > info.calc_turnover_pnl_currency(params.stop_price, params.quantity)) {
                return update_order(order,OrderRejectionReason::too_small);
            }
        }
    }

    _active_orders.push_back({order, calc_turnover(params, TradableInstrument(shared_from_this()),_last_price, 0),{}});
    if (!update_margin()) {
        return update_order(order,OrderRejectionReason::insufficient_funds);
        _active_orders.pop_back();    
        update_margin();
    }

    get_sim_instrument()->get_sim_exchange()->place_order(order);    
            
}
bool SimTradableInstrument::need_local_trigger(OrderType ) const {
    return false;
}


}




