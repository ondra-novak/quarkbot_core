#pragma once

#include "account_base.hpp"
#include "order_internal_defs.hpp"
#include "order_trigger.hpp"
#include "orderdata.hpp"
#include "quarkbot/abstract/ieventstream.hpp"
#include "quarkbot/abstract/iexchange.hpp"
#include "quarkbot/abstract/itradable_instrument.hpp"
#include "quarkbot/abstract/imarket_instrument.hpp"
#include "quarkbot/defs.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/strategy_publisher.hpp"
#include <memory>
#include <shared_mutex>

namespace quarkbot {

    class TradableInstrumentBase: public ITradableInstrument, public std::enable_shared_from_this<TradableInstrumentBase> {
    public:
        TradableInstrumentBase(std::shared_ptr<IMarketInstrument> instrument,  std::shared_ptr<IAccount> account);

        virtual POrder place_order(const OrderRequest &params, POrder order_to_replace, std::size_t param_class_hash) override;
        virtual OrderParameters convert_request_to_params(const OrderRequest &req, Side cur_position_side) const override{
            const auto &info = get_instrument()->get_info();
            int aps = static_cast<int>(req.side);
            int aqs = req.side == cur_position_side?1:-1;
            return {
                req.label,
                req.side,
                req.type,
                req.quantity.get_rounded(info.quantity_increment, aqs),
                req.limit_price.get_rounded(info.price_increment, aps),
                req.stop_price.get_rounded(info.price_increment, aps),
                req.time_in_force,
                req.leverage,
                req.reduce_only,
                req.hedge,
                req.local_trigger,
                req.keep_alive,
                req.reason_override
            };
        };
        
        //return true if fill has been used, or false in case of duplicity
        bool update_order(const POrderData &order, Fill &&fill);
        void update_order(const POrderData &order, const OrderStatus &status);
        void update_order(const POrderData &order, const OrderRejectionReason &status);
        void update_order(const POrderData &order, OrderRejectionWithText &&status);
        void update_order(const POrderData &order, OrderOpenStatus &&status);
        void update_order(const POrderData &order, const OrderFillStats &status);

        virtual bool attach_storage(PStorage , StorageConfig , function_view<void(const Order &)> ) override {
            return false;
        }

        bool attach_storage_impl(PStorage storage, StorageConfig cfg, function_view<void(const Order &)> order_callback);

        ///create order instance - order factory 
        virtual POrderData create_order(const OrderParameters &params, POrder replaced_order, std::size_t class_hash) = 0;
        ///validate and place order;
        virtual void submit_order(POrderData order) = 0;
        ///determine, whether exchange needs local trigger for this type of order
        virtual bool need_local_trigger(OrderType type) const {return type == OrderType::alert;}

        virtual PAccount get_account() const override {return _account;}
        virtual PMarketInstrument get_instrument() const override {return _instrument;};

        ///serialize order to the binary blob
        /**
            @param ord order to serialize. Note parameters and ID is always serialized, so adapter just need to serialize extra data
            @return serialized blob
            @note default function is empty
         */
        virtual std::vector<unsigned char> serialize_order(const POrderData &) const {return {};}
        ///deserialize order
        /**
            @param id id of order
            @param parameters order parameters
            @param serialized blob
            @return new order. 
            @note default function calls create_order, and restore_order. The function ignores serialized blob, as default serialize_order is also empty
         */
        virtual POrderData deserialize_order(std::string id, const OrderParameters &parameters, std::span<const unsigned char> serialized) ;
        ///Function called from default implementation deserialize_order in order to find status of the order on the exchange
        /**
        @param order order

        @note function can explore status of order asynchronously, because status is broadcasted as and update
         */
        virtual void restore_order(POrderData order);

    protected:
        std::shared_ptr<IMarketInstrument> _instrument;
        std::shared_ptr<IAccount> _account;
        OrderTrigger _trigger;
        Storage _storage;
        StorageConfig _storage_config;
        

        void open_order(const POrderData &ord);
        void close_order(const POrderData &ord);


    };

    

}