#include "tradable_instrument_base.hpp"
#include "order_internal_defs.hpp"
#include "quarkbot/order.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/storage_srl.hpp"
#include "quarkbot/types.hpp"
#include <chrono>
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

    bool TradableInstrumentBase::update_order(const POrderData &order, Fill &&fill) {
        if (_storage) {
            auto v = _storage.get(_storage_config.fills_key,fill.key);
            if (v.exists) return false;
            auto trn = _storage.write();
            trn.store(_storage_config.fills_key,fill.key, fill);
            trn.commit();
        }
        _account->on_order_event({order}, fill);
        order->update(std::move(fill));        
        return true;
    }
    void TradableInstrumentBase::update_order(const POrderData &order, const OrderStatus &status) {
        if (_storage) {
            if (status == OrderStatus::open) open_order(order);
            else if (is_done_status(status)) close_order(order);            
        }
        _account->on_order_event({order}, status);                
        order->update(status);        
    }
    void TradableInstrumentBase::update_order(const POrderData &order, const OrderRejectionReason &status) {
        if (_storage) close_order(order);
        _account->on_order_event({order}, OrderStatus::rejected);
        order->update(status);        
    }
    void TradableInstrumentBase::update_order(const POrderData &order, OrderRejectionWithText &&status) {
        if (_storage) close_order(order);
        _account->on_order_event({order}, OrderStatus::rejected);
        order->update(std::move(status));        
    }
    void TradableInstrumentBase::update_order(const POrderData &order, OrderOpenStatus &&status) {
        order->update(std::move(status));        
        if (_storage) open_order(order);
        _account->on_order_event({order}, OrderStatus::open);
    }
    void TradableInstrumentBase::update_order(const POrderData &order, const OrderFillStats &status) {
        order->update(status);
        if (_storage) {
            auto wr = _storage.write();
            OrderFillStatsRecord stats_rec;
            OrderFillStats &stats = stats_rec;
            stats = status;
            stats_rec.contract = get_instrument()->get_info();
            stats_rec.timestamp = std::chrono::system_clock::now();
            stats_rec.label = order->get_parameters().label;
            wr.store(_storage_config.trades_key, order->get_key(), stats_rec);
            wr.commit();
        }
    }

    struct SerializedOrder {
        std::string id;
        OrderParameters params;        
        std::vector<unsigned char> serialized;
        void serialize(this auto &self, auto &arch) {
            arch(self.id, "id");
            arch(self.params, "params");
            arch(self.serialized, "data");
        }
    };

    void TradableInstrumentBase::open_order(const POrderData &ord){
        SerializedOrder srl {
                    std::string(ord->get_id()),
                    ord->get_parameters(),
                    this->serialize_order(ord)
        };
        auto tr = _storage.write();
        auto key = ord->get_key();
        tr.store(_storage_config.active_orders_key, key, srl);
        tr.commit();
    }
    void TradableInstrumentBase::close_order(const POrderData &ord){
        auto tr = _storage.write();
        auto key = ord->get_key();
        tr.erase(_storage_config.active_orders_key, key);
        tr.commit();
    }


    bool TradableInstrumentBase::attach_storage_impl(PStorage storage, StorageConfig cfg, function_view<void(const Order &)> order_callback) {
        this->_storage_config = cfg;
        this->_storage = std::move(storage);

        SerializedOrder srl;
        OrderFillStatsRecord rec;

        for (const auto &v: this->_storage.select_range(_storage_config.active_orders_key, RecordKey::min(), RecordKey::max())) {
            if (v.extract(srl)) {
                auto ord = deserialize_order(std::move(srl.id), srl.params, srl.serialized);                
                if (ord) {
                    auto v2 = this->_storage.get(_storage_config.trades_key, ord->get_key());
                    if (v2.exists && v2.extract(rec)) {
                        ord->update(rec);
                    }
                    order_callback(Order(ord));
                }
            }
        }
        return true;
    }

    void TradableInstrumentBase::restore_order(POrderData order) {        
        order->update(OrderStatus::lost);
    }

    POrderData TradableInstrumentBase::deserialize_order(std::string id, const OrderParameters &parameters, std::span<const unsigned char> )  {
        auto ord = this->create_order(parameters, {}, {});
        ord->set_id(std::move(id));
        restore_order(ord);
        return ord;
    }



}