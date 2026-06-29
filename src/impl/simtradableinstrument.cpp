#include "simtradableinstrument.hpp"
#include "ifc/abstract/orderdata.hpp"
#include "ifc/execution_worker.hpp"
#include "ifc/order.hpp"
#include "ifc/order_storage.hpp"
#include "ifc/tradable_instrument.hpp"
#include "ifc/types.hpp"
#include "simaccount.hpp"
#include "siminstrument.hpp"
#include "utils/decimal.hpp"
#include <cassert>
#include <memory>
#include <optional>
#include <stdexcept>
#include <variant>
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


std::unique_ptr<IEventStreamBase> SimTradableInstrument::subscribe_stream(std::size_t hash, const void *) {
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
            liquidation_order = place_order({
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
            },{},{},0);
        }
    }
}

std::shared_ptr<OrderInternalData>  SimTradableInstrument::place_order(const OrderRequest &req, std::shared_ptr<OrderInternalData> old_state, std::string_view name, std::size_t) {


    ExecutionWorker worker = ExecutionWorker::current();    
    auto st = OrderInternalData::create(
        convert_request_to_params(req, _position.side),
        shared_from_this(),
        std::string(name),
        old_state,
        worker.required().now(),
        _order_storage
    );
    st->set_cancel_fn([me = shared_from_this()](OrderInternalData *order){
            me->_instrument->get_sim_exchange()->cancel_order(order);
    });


    const auto &info = _instrument->get_info();    


    const auto &params = st->get_parameters();
    if (params.side != Side::buy && params.side != Side::sell) {
        st->update(OrderRejectionWithText{ OrderRejectionReason::invalid_params,"Invalid side"});
        return st;
    }
    if (params.quantity < info.min_quantity) {
        st->update(OrderRejectionReason::too_small);
        return st;
    }
    if (is_limit_order(params.type) || is_stop_order(params.type)) {
        if (is_limit_order(params.type)) {
            if (params.limit_price <= 0) {
                st->update(OrderRejectionWithText{ OrderRejectionReason::invalid_params, "Missing limit price"});
                return st;
            }
            if (info.min_turnover > info.calc_turnover_pnl_currency(params.limit_price, params.quantity)) {
                st->update( OrderRejectionReason::too_small);
                return st;
            }
        }
        if (is_stop_order(params.type)) {
            if (params.stop_price <= 0) {
                st->update(OrderRejectionWithText{ OrderRejectionReason::invalid_params, "Missing limit price"});
                return st;
            }
            if (info.min_turnover > info.calc_turnover_pnl_currency(params.stop_price, params.quantity)) {
                st->update(OrderRejectionReason::too_small);
                return st;
            }
        }
    }

    _active_orders.push_back({st, calc_turnover(params, TradableInstrument(shared_from_this()),_last_price, 0),{}});
    if (!update_margin()) {
        st->update( OrderRejectionReason::insufficient_funds);
        _active_orders.pop_back();    
        update_margin();
        return st;
    }

    _instrument->get_sim_exchange()->place_order(st);    
    return st;
            
}


void SimTradableInstrument::on_order_update(POrderAData ord, OrderInternalData::Update &&status) {
   
    if (std::holds_alternative<Fill>(status)) {
        const Fill &f = std::get<Fill>(status);
        report_fill(f);
    } else {
        if ((std::holds_alternative<OrderStatus>(status) && is_done_status(std::get<OrderStatus>(status)))
            || std::holds_alternative<OrderRejectionReason>(status)
            || std::holds_alternative<OrderRejectionWithText>(status)) {
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
    }
    ord->forward_update(std::move(status));
}

std::vector<Order> SimTradableInstrument::attach_storage(PStorage storage, std::string key_name) {
    _order_storage = std::make_shared<OrderStorage>(storage, key_name);
    ExecutionWorker worker = ExecutionWorker::current();
    std::vector<Order> out;

    auto orders = _order_storage->load_opened_orders();
    for (auto &s: orders) {
        auto adata = OrderInternalData::create(
            s.st.parameters,
            shared_from_this(),
            s.st.name,
            std::weak_ptr<OrderInternalData>(),
            worker.required().now(),
            _order_storage
        );
        adata->set_restored_data(s.st.id,s.fill_st);
        adata->set_cancel_fn([this](OrderInternalData *order){
                    _instrument->get_sim_exchange()->cancel_order(order);
        });
        Order ord(std::move(adata));
        out.push_back(ord);
    }

    //TODO: register orders to sim exchange
    return out;

}


}




