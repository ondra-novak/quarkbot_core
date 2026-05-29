#pragma once

#include "ifc/abstract/itradable_instrument.hpp"
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include "ifc/order_defs.hpp"
#include "ifc/order_storage.hpp"
#include <memory>
#include <mutex>
#include <variant>
namespace quarkbot {

    static constexpr OrderParameters empty_order_parameters = {};


    struct OrderAdapterData;
    using POrderAData = std::shared_ptr<OrderAdapterData>;

    ///shared structure
    struct OrderStrategyData {
        
        ///internal order ID
        std::string id = {};
        ///filled amount (calculated locally)
        Decimal filled = {};
        ///total turnover
        Decimal turnover = {};
        ///order status
        OrderStatus status = OrderStatus::sent; 
        ///if order rejected, there is reject reason
        OrderRejectionReason reject_reason = {};
        ///if order rejected, there is message if any
        std::string rejection_message = {};
        ///all fills extracted from the queue;
        std::deque<Fill> fills;
        ///keep_alive order - will not send close on destroy
        bool keep_alive = false;
        
        std::shared_ptr<OrderAdapterData> adapter_data;

        OrderStrategyData() = default;
    
        void cancel();

        ~OrderStrategyData() {
            if (!keep_alive) cancel();    
        }

         static std::shared_ptr<OrderStrategyData> create() {
                return std::shared_ptr<OrderStrategyData>();
         }          

         void apply_update(Fill &&up) {
            filled += up.amount;
            turnover += up.amount * up.price;
            fills.push_back(std::move(up));
        }
         void apply_update(OrderStatus up) {status = up;}
         void apply_update(OrderRejectionReason rsn) {status = OrderStatus::rejected; reject_reason = rsn;}
         void apply_update(OrderRejectionWithText &&rsn) {
            status = OrderStatus::rejected; reject_reason = rsn.reason; 
            rejection_message = std::move(rsn.text);
        }
        void apply_update(OrderOpenStatus &&opn) {
            status = OrderStatus::open;
            id = std::move(opn.id);
        }
        void apply_update(POrderAData other);

    };



    struct OrderAdapterData {
        
        using Update = std::variant<Fill, OrderStatus, OrderRejectionReason, OrderRejectionWithText,  OrderOpenStatus, POrderAData>;
 
        
        std::weak_ptr<OrderStrategyData> strategy_data = {};

        const OrderParameters parameters = {};
        ///associated instrument
        std::shared_ptr<ITradableInstrument> instrument = {};
        ///order name
        std::string name = {};
        ///reference to replaced order
        std::weak_ptr<OrderStrategyData> replaced_order = {};
        ///time, when this order has been created (or restored)
        std::chrono::system_clock::time_point create_time = {};
        ///adapter's generated unique record key - this can be used to store into database
        RecordKey key = {};
        ///internal order ID
        std::string id = {};
        ///associated order storage
        std::shared_ptr<OrderStorage> storage = {};
        ///cancel function - must be filled by adapter
        std::function<void(OrderStrategyData &)> cancel = {};

        std::recursive_mutex mx;
        ///queue of updates
        std::vector<Update> updates;
        ///awaiting coroutine
        ResultAndExecWorker<bool> awaiting = {};
        ///order has been canceled internally - reserved for adapter
        std::atomic<bool> canceled = {};

        OrderStorage::FilledState fst = {};

        OrderAdapterData(std::shared_ptr<OrderStrategyData> strategy_data,
            const OrderParameters &parameters,
            std::shared_ptr<ITradableInstrument> instrument,
            std::string name,
            std::weak_ptr<OrderStrategyData> replaced_order,            
            std::chrono::system_clock::time_point create_time,
            std::function<void(OrderStrategyData &)> cancel_fn,
            std::shared_ptr<OrderStorage> storage
)
            :strategy_data(strategy_data)
            ,parameters(parameters)
            ,instrument(std::move(instrument))
            ,name(std::move(name))
            ,replaced_order(std::move(replaced_order))
            ,create_time(std::move(create_time))
            ,storage(std::move(storage))
            ,cancel(std::move(cancel_fn)) {}

        static std::shared_ptr<OrderAdapterData> create(std::shared_ptr<OrderStrategyData> strategy_data,
                                                const OrderParameters &parameters,
                                                std::shared_ptr<ITradableInstrument> instrument,
                                                std::string name,
                                                std::weak_ptr<OrderStrategyData> replaced_order,            
                                                std::chrono::system_clock::time_point create_time,
                                                std::function<void(OrderStrategyData &)> cancel_fn,
                                                std::shared_ptr<OrderStorage> storage
                                            ) 
        {
            std::shared_ptr<OrderAdapterData> out = std::make_shared<OrderAdapterData>(strategy_data,
                parameters, std::move(instrument),std::move(name),
                std::move(replaced_order),
                std::move(create_time),
                std::move(cancel_fn),
                std::move(storage)
            );
            strategy_data->adapter_data = out;
            return out;
        }

        void update(Fill &&up) {
            if (storage) {
                if (storage->check_fill_exists(up)) return;
                auto wr = storage->write();
                storage->store_fill(wr, up);
                fst.filled += up.amount;
                fst.turnover += up.amount*up.price;
                storage->store_filled(wr,key, fst);
                wr->commit();
            }
            std::scoped_lock _(mx);
            updates.push_back(std::move(up));
            notify();
        }

        void update(OrderStatus st) {
            if (storage && is_done_status(st)) {
                auto wr = storage->write();
                storage->close_order(wr, key);
            }
            std::scoped_lock _(mx);
            updates.push_back(st);
            notify();
        }

        void update(OrderRejectionReason st) {
            if (storage) {
                auto wr = storage->write();
                storage->close_order(wr, key);
            }
            std::scoped_lock _(mx);
            updates.push_back(st);
            notify();
        }
        void update(OrderRejectionWithText &&st) {
            if (storage) {
                auto wr = storage->write();
                storage->close_order(wr, key);
            }
            std::scoped_lock _(mx);
            updates.push_back(st);
            notify();
        }

        void update(OrderOpenStatus &&st) {
            if (storage) {
                auto wr = storage->write();
                storage->open_order(wr, st.key, {st.id,name,parameters});
            }
            std::scoped_lock _(mx);
            id = st.id;
            key = st.key;
            updates.push_back(st);
            notify();
        }
        void update(POrderAData to) {
            std::scoped_lock _(mx);
            to->strategy_data = strategy_data;
            updates.push_back(to);
            notify();
        }
        void forward_update(Update &&up) {
            std::visit([&](auto &x){this->update(std::move(x));},up);
        }

        bool flush_updates() {
            bool res = false;
            auto order = this->strategy_data.lock();
            if (!order) return res;
            std::scoped_lock _(mx);
            
            for (auto &x: updates) {
                std::visit([&](auto &up){
                    order->apply_update(std::move(up));
                    res = true;
                },x);
            }
            updates.clear();
            return res;
        }

        void notify() {
            if (awaiting) {
                flush_updates();
                awaiting(true);
            }
        }

        bool add_awaiter(awaitable<bool>::result &awt) {
            std::scoped_lock _(mx);
            if (updates.empty()) {
                awaiting = ResultAndExecWorker<bool>(std::move(awt));
                return true;
            }
            flush_updates();
            return false;
        }        

    };

    inline void OrderStrategyData::cancel() {        
        if (!is_done_status(status)) {
            adapter_data->cancel(*this);
        }
    }
    inline void OrderStrategyData::apply_update(POrderAData other) {
        adapter_data = other;
    }

 
}