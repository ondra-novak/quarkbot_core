#include "tradable_instrument_base.hpp"
#include "order_internal_defs.hpp"
#include "quarkbot/order.hpp"
#include "quarkbot/order_defs.hpp"
#include <memory>


namespace quarkbot {
    TradableInstrumentBase::TradableInstrumentBase(std::shared_ptr<IMarketInstrument> instrument,  std::shared_ptr<IAccount> account)
        :_instrument(std::move(instrument))
        ,_account(std::move(account))
    {

    }

    POrder TradableInstrumentBase::place_order(const OrderRequest &req, POrder order_to_replace, std::size_t param_class_hash) {       
        auto params = this->convert_request_to_params(req, req.side);
        if (params.local_trigger || need_local_trigger(params.type)) {
            auto ord = _trigger.place_order(shared_from_this(), params, order_to_replace);
            return ord;
        } else {
            auto ord = create_order(
                params,
                order_to_replace,
                param_class_hash);
            auto chk = _account->pre_trade_check(Order{ord});
            if (!chk.ok) {
                ord->update(OrderRejectionWithText{chk.rej_reason, chk.rej_message});
                return ord;
            }
            submit_order(ord);
            return ord;
        }
    }

    void TradableInstrumentBase::update_order(const POrderData &order, Fill &&fill) {
        _account->on_order_event({order}, fill);
        order->update(std::move(fill));        
    }
    void TradableInstrumentBase::update_order(const POrderData &order, const OrderStatus &status) {
        _account->on_order_event({order}, status);
        order->update(status);        
    }
    void TradableInstrumentBase::update_order(const POrderData &order, const OrderRejectionReason &status) {
        _account->on_order_event({order}, OrderStatus::rejected);
        order->update(status);        
    }
    void TradableInstrumentBase::update_order(const POrderData &order, OrderRejectionWithText &&status) {
        _account->on_order_event({order}, OrderStatus::rejected);
        order->update(std::move(status));        
    }
    void TradableInstrumentBase::update_order(const POrderData &order, OrderOpenStatus &&status) {
        _account->on_order_event({order}, OrderStatus::open);
        order->update(std::move(status));        
    }
    void TradableInstrumentBase::update_order(const POrderData &order, const OrderFillStats &status) {
        order->update(status);
        //no publish, no fill or change status
    }


}