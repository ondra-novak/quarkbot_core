#include "simtradableinstrument.hpp"
#include "ifc/execution_worker.hpp"
#include "ifc/order.hpp"
#include "ifc/streaming.hpp"
#include "ifc/tradable_instrument.hpp"
#include "ifc/types.hpp"
#include "ifc/context.hpp"
#include "siminstrument.hpp"
#include "simaccount.hpp"
#include "utils/decimal.hpp"
#include <cassert>
#include <memory>
#include <optional>
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


std::unique_ptr<IEventStreamBase> SimTradableInstrument::subscribe_stream_internal(std::string_view type, const StreamParams *) {
    if (type == OrderEvent::type) {
        auto s = _order_update_stream.lock();
        if (s == nullptr) {
            s = std::make_shared<QueueEventPublisher<OrderEvent> >();
            _order_update_stream = s;
        }
        return s->create_subscriber();
    } else if (type == ExternalFill::type) {
        auto s = _liquidation_stream.lock();
        if (s == nullptr) {
            s = std::make_shared<QueueEventPublisher<ExternalFill> >();
            _liquidation_stream = s;
        }
        return s->create_subscriber();
    }
    return nullptr;
}
PAccount SimTradableInstrument::get_account() const {
        return _account;
    }
PMarketInstrument SimTradableInstrument::get_instrument() const {
        return _instrument;
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
                false,
                TimeInForce::gtc,
                ExecutionReason::liquidation
            }));
        }
    }
}

Order SimTradableInstrument::place_order(const OrderRequest &req, std::shared_ptr<OrderEx::State> old_state, std::string_view name) {

    auto st = std::make_shared<OrderEx::State>(
        convert_request_to_params(req, _position.side),
        shared_from_this(),
        std::string(name),
        old_state
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

    _active_orders.push_back({
        new_order, new_order.get_turnover(_last_price), {}
    });
    if (!update_margin()) {
        new_order.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::insufficient_funds});
        _active_orders.pop_back();    
        update_margin();
        return new_order;
    }

    _instrument->get_sim_exchange()->place_order(new_order);    
    return new_order;
            
}

 
SerializedOrder SimTradableInstrument::serialize_order(Order ) {
    return "//todo";
}
Order SimTradableInstrument::restore_order(SerializedOrder ) {

    Order out(std::make_shared<OrderEx::State>(OrderParameters{}, shared_from_this(),std::string("//todo"),  std::weak_ptr<OrderEx::State>()));
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
    auto s = _order_update_stream.lock();
    report_fill(fill);

    if (s) {
        s->publish({{}, ord.get_id(), serialize_order(o), fill});        
    }
    if (ord == liquidation_order) {
        auto lq = _liquidation_stream.lock();
        if (lq) {                        
            lq->publish({fill});
        }
    }
    o.update_order(std::move(fill));;
}

void SimTradableInstrument::on_order_status(const Order &ord, const OrderStatusUpdate &status){
    OrderEx o(ord);    
    auto st = o.get_state();
    auto s = _order_update_stream.lock(); 
    if (s) {
        s->publish({{}, ord.get_id(), 
            is_done_status(status.status)?std::optional<std::string>(std::nullopt):std::optional<std::string>(serialize_order(o)),
            {}
        });
    }
    if (is_done_status(status.status)) {
        if (ord == liquidation_order) {
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
    o.update_order(status);
}


}




