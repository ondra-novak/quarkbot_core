#pragma once

#include "../defs.hpp"
#include "../execution_worker.hpp"
#include "../order_defs.hpp"
#include "../order_storage.hpp"
#include "../types.hpp"
#include "basic_coro/prepared_coro.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <type_traits>
#include <variant>
namespace quarkbot {



    static constexpr OrderParameters empty_order_parameters = {};

    class OrderInternalData;
    class ITradableInstrument;
    using POrderAData = std::shared_ptr<OrderInternalData>;


    struct OrderAwaiting {
        OrderReport &report;
        POrderAData &ptr_to_redirect;
        awaitable<bool>::result result;
        enum RegisterStatus {
            //awaiter registered
            accepted,
            //reject register, order is done
            rejected_done,
            //reject register, there are updates
            rejected_updates,
            //reject register - already pending
            rejected_pending
        };
    };

    constexpr OrderStatus rejection_reason_2_status(OrderRejectionReason rej) {    
        return (rej == OrderRejectionReason::expired || rej == OrderRejectionReason::post_only_taker)
            ?OrderStatus::canceled:OrderStatus::rejected;
    }


    class OrderInternalData {
    public:
        //update definition

        using Update = std::variant<Fill, OrderStatus, OrderRejectionReason, OrderRejectionWithText,  OrderOpenStatus>;
        using CancelFn = std::function<void(OrderInternalData *)>;

        

        static OrderStatus update2status(const Update &up) {
            return std::visit([]<typename T>(const T &x){
                if constexpr(std::is_same_v<T, OrderStatus>) {
                    return x;
                } else if constexpr(std::is_same_v<T, OrderRejectionReason>) {
                    return rejection_reason_2_status(x);
                } else if constexpr(std::is_same_v<T, OrderRejectionWithText>) {
                    return rejection_reason_2_status(x.reason);                    
                } else if constexpr(std::is_same_v<T, OrderOpenStatus>) {
                    return OrderStatus::open;
                } else {
                    static_assert(std::is_same_v<T,Fill>);
                    return OrderStatus::open;
                }
            }, up);
        }

        
        OrderStorage::FilledState fst = {};
        OrderInternalData(
            OrderParameters parameters,
            std::shared_ptr<ITradableInstrument> instrument,
            std::string name,
            std::weak_ptr<OrderInternalData> replaced_order,            
            std::chrono::system_clock::time_point create_time,
            std::shared_ptr<OrderStorage> storage)
                :parameters(std::move(parameters))
                ,instrument(std::move(instrument))
                ,name(std::move(name))
                ,replaced_order(std::move(replaced_order))
                ,create_time(std::move(create_time))
                ,storage(std::move(storage))
                 {
                    updates_target = this;
                }

        virtual ~OrderInternalData() {
            if (!done && !keep_alive.load(std::memory_order_relaxed)) {
                cancel();
            }
        }
        OrderInternalData(const OrderInternalData &) = delete;
        OrderInternalData &operator=(const OrderInternalData &) = delete;

        static std::shared_ptr<OrderInternalData> create(OrderParameters parameters,
                                                std::shared_ptr<ITradableInstrument> instrument,
                                                std::string name,
                                                std::weak_ptr<OrderInternalData> replaced_order,            
                                                std::chrono::system_clock::time_point create_time,                
                                                std::shared_ptr<OrderStorage> storage
                                            ) 
        {
            return  std::make_shared<OrderInternalData>(
                            std::move(parameters), 
                            std::move(instrument),
                            std::move(name),
                            std::move(replaced_order),
                            std::move(create_time),                
                            std::move(storage)
                        );
        }


        coro::prepared_coro update(Fill &&up) {             
            if (storage) {
                if (storage->check_fill_exists(up)) return {};
                auto wr = storage->write();
                storage->store_fill(wr, up);
                fst.filled += up.amount;
                fst.turnover += up.amount*up.price;
                storage->store_filled(wr,key, fst);
                wr->commit();
            }
            std::scoped_lock _(updates_target->mx);
            updates_target->updates.push_back(std::move(up));
            return notify_lk();
        }

        coro::prepared_coro update(OrderStatus st) {
            if (storage && is_done_status(st)) {
                auto wr = storage->write();
                storage->close_order(wr, key);
            }
            std::scoped_lock _(updates_target->mx);
            updates_target->updates.push_back(st);
            updates_target->done = updates_target->done || is_done_status(st);
            return updates_target->notify_lk();
        }

        coro::prepared_coro update(OrderRejectionReason st) {
            if (storage) {
                auto wr = storage->write();
                storage->close_order(wr, key);
            }
            std::scoped_lock _(updates_target->mx);
            updates_target->updates.push_back(st);
            updates_target->done = true;
            return updates_target->notify_lk();
        }
        coro::prepared_coro update(OrderRejectionWithText &&st) {
            if (storage) {
                auto wr = storage->write();
                storage->close_order(wr, key);
            }
            std::scoped_lock _(updates_target->mx);
            updates.push_back(std::move(st));
            updates_target->done = true;
            return updates_target->notify_lk();
        }

        coro::prepared_coro update(OrderOpenStatus &&st) {
            if (storage) {
                auto wr = storage->write();
                storage->open_order(wr, st.key, {st.id,name,parameters});
            }
            std::scoped_lock _(updates_target->mx);
            id = updates_target->id = st.id;
            key = updates_target->key = st.key;
            updates_target->updates.push_back(OrderStatus::open);
            updates_target->done = false;
            return updates_target->notify_lk();
        }
        coro::prepared_coro forward_update(Update &&up) {
            return std::visit([&](auto &x){return this->update(std::move(x));},up);
        }

        ///flush updates to report
        /**
            @param report target report
            @param reference to pointer to new OrderAdatapterData - used by trigger to point to a new order
            @retval true any updates applied
            @retval false no updated
         */
        bool flush_updates(OrderReport &report, POrderAData &redirect_ptr) {
            std::scoped_lock _(mx);
            return flush_updates_lk(report, redirect_ptr);
        }

        ///register awaiter
        /**
            @param report reference to report. Ensure reference valid during waiting
            @param redirect_ptr reference to order pointer, to handle redirect and triggers, ensure it is valud during waiting
            @param promise reference to result promise. When operation is approved, the result promise is moved to internal state, otherwise
                it stays untouched
            @retval true approved await operation, promise has been moved
            @retval false rejected await operation, there already updates processed, report is updated, promise is untouched
        */
        OrderAwaiting::RegisterStatus register_awaiter(OrderReport &report, POrderAData &redirect_ptr, awaitable<bool>::result &promise) {
            std::scoped_lock _(mx);
            return register_awaiter_lk(report, redirect_ptr, promise);
        }

        bool is_done_lk() const {
            return done;
        }
        
        bool is_done() const {
            std::scoped_lock _(mx);
            return done && updates.empty();
        }

        const OrderParameters &get_parameters() const {return parameters;}
        std::shared_ptr<ITradableInstrument> get_instrument() const {return instrument;}
        const std::string &get_name() const {return name;}
        std::weak_ptr<OrderInternalData> get_replaced_order() const {return replaced_order;}
        std::chrono::system_clock::time_point get_create_time() const {return create_time;}
        void set_keep_alive(bool kp = true) {keep_alive.store(kp, std::memory_order_relaxed);}
        bool is_keep_alive() const {return keep_alive.load(std::memory_order_relaxed);}
        void cancel() {if (cancel_fn) [[likely]] cancel_fn(this);}

        ///set cancel callback
        /**
            The provider must set cancel callback before the order is emited to the frontend, otherwise operation is not MT
        */
        void set_cancel_fn(CancelFn fn) {cancel_fn = std::move(fn);};
        ///Redirects updates to other order data
        /**
            This used to implement triggered order. Original order defintion is held by trigger engine while
            updates from created order after trigger are forwarded into original order.            
         */
        void redirect_updates(OrderInternalData *to) {
            std::scoped_lock _(mx);
            updates_target = to;
            for (auto &x: updates) to->forward_update(std::move(x));
            to->done = done;
        }

        ///mark canceled
        /**
        @retval true canceled
        @retval false already cancel before
        */
        bool mark_canceled() {
            return !canceled.exchange(true, std::memory_order_relaxed);
        }
        
        void set_restored_data(std::string id, OrderStorage::FilledState fst) {
            std::scoped_lock _(mx);
            this->id = std::move(id);
            this->fst = fst;
        }

        const std::string &get_id() const {return id;}

        const std::string &get_id_unsafe() const {
            static std::string empty_id;
            std::scoped_lock _(mx);
            //if id is empty - return empty id reference
            if (id.empty()) return empty_id;
            //otherwise we can return valid object as the object won't be changed
            else return id;

        }
        void set_id(std::string id) {this->id = std::move(id);}
        Decimal get_remaining_quantity() const {return parameters.quantity - fst.filled;}

    protected:
        ///current order parameters
        const OrderParameters parameters = {};
        ///associated instrument
        std::shared_ptr<ITradableInstrument> instrument = {};
        ///order name
        std::string name = {};
        ///reference to replaced order
        std::weak_ptr<OrderInternalData> replaced_order = {};
        ///time, when this order has been created (or restored)
        std::chrono::system_clock::time_point create_time = {};
        ///adapter's generated unique record key - this can be used to store into database
        RecordKey key = {};
        ///internal order ID
        std::string id = {};
        ///associated order storage
        std::shared_ptr<OrderStorage> storage = {};
        ///cancel function - must be filled by adapter
        CancelFn cancel_fn = {};

        OrderInternalData *updates_target;

        mutable std::mutex mx;
        ///queue of updates
        std::vector<Update> updates;
        ///awaiting coroutine
        std::optional<OrderAwaiting> awaiting;
        ///order has been canceled internally - reserved for adapter
        std::atomic<bool> canceled = {};
        ///order has been marked keep alive - it will not be canceled when this structure is dropped
        std::atomic<bool> keep_alive = {};
        //order is done
        bool done = false;


        struct ApplyUpdateVisitor {
            OrderReport &report;
            POrderAData &redirect_ptr;

            void operator()(Fill &fill) {
                report.fills.push_back(std::move(fill));
            }
            void operator()(OrderStatus &st) {
                report.status = st;
                report.status_changed = true;
            }
            void operator()(OrderRejectionReason  &rej) {
                report.status = rejection_reason_2_status(rej);
                report.rejection_reason = rej;
                report.rejection_message.clear();
                report.status_changed = true;
            }
            void operator()(OrderRejectionWithText  &rej) {
                report.status = rejection_reason_2_status(rej.reason);
                report.rejection_reason = rej.reason;
                report.rejection_message = std::move(rej.text);                
                report.status_changed = true;
            }
            void operator()(POrderAData &adata) {
                redirect_ptr = std::move(adata);
            }
            void operator()(OrderOpenStatus &) {
                report.status = OrderStatus::open;
                report.status_changed = true;
            }
        };

        ///flush updates to report (internally locked by mutex)
        /**
            @param report target report
            @param reference to pointer to new OrderAdatapterData - used by trigger to point to a new order
            @retval true any updates applied
            @retval false no updated
         */
        bool flush_updates_lk(OrderReport &report, POrderAData &redirect_ptr) {
            bool ok = false;
            ApplyUpdateVisitor visitor{report,redirect_ptr};
            for (auto &x: updates) {
                ok = true;
                std::visit(visitor, x);                
            }
            updates.clear();
            report.filled = fst.filled;
            report.turnover = fst.turnover;
            return ok;
        }


        ///register awaiter
        /**
            @param report reference to report. Ensure reference valid during waiting
            @param redirect_ptr reference to order pointer, to handle redirect and triggers, ensure it is valud during waiting
            @param promise reference to result promise. When operation is approved, the result promise is moved to internal state, otherwise
                it stays untouched
            @retval true approved await operation, promise has been moved
            @retval false rejected await operation, there already updates processed, report is updated, promise is untouched
        */
        OrderAwaiting::RegisterStatus register_awaiter_lk(OrderReport &report, POrderAData &redirect_ptr, awaitable<bool>::result &promise) {
            //nobody should await yet
            if (awaiting) return OrderAwaiting::rejected_pending;

            //try to flush updates
            if (flush_updates_lk(report, redirect_ptr)) return OrderAwaiting::rejected_updates;

            if (done) return OrderAwaiting::rejected_done;

            //register promise and arguments
            awaiting.emplace(OrderAwaiting{report, redirect_ptr, 
                                           std::move(promise)});
            //awaiting started
            return OrderAwaiting::accepted;
                            
        }
        ///notify about update (locked)
        coro::prepared_coro notify_lk() {
            coro::prepared_coro out;
            //any awaiting
            if (awaiting.has_value()) {
                //flush updates
                if (flush_updates_lk(awaiting->report, awaiting->ptr_to_redirect)) {
                    //in case success, resolve promise
                    out = awaiting->result(true);
                    //reset internal state
                    awaiting.reset();
                }
            }
            return out;
        }

    };

 
}

