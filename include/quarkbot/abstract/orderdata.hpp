#pragma once

#include "../defs.hpp"
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
    using POrderData = std::shared_ptr<OrderInternalData>;
    using OrderData = OrderInternalData;

    enum class OrderReceiveReportStatus {
        awaiting,
        done,
        updates,
        already_pending,
        need_await

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

        
        OrderInternalData(
            OrderParameters parameters,
            std::shared_ptr<ITradableInstrument> instrument,
            std::weak_ptr<OrderInternalData> replaced_order,            
            std::chrono::system_clock::time_point create_time,
            std::shared_ptr<OrderStorage> storage)
                :parameters(std::move(parameters))
                ,instrument(std::move(instrument))
                ,replaced_order(std::move(replaced_order))
                ,create_time(std::move(create_time))
                ,storage(std::move(storage))
                 {                  
                }

        virtual ~OrderInternalData() {
            if (!done && !parameters.keep_alive) {
                cancel();
            }
        }
        OrderInternalData(const OrderInternalData &) = delete;
        OrderInternalData &operator=(const OrderInternalData &) = delete;

        static std::shared_ptr<OrderInternalData> create(OrderParameters parameters,
                                                std::shared_ptr<ITradableInstrument> instrument,
                                                std::weak_ptr<OrderInternalData> replaced_order,            
                                                std::chrono::system_clock::time_point create_time,                
                                                std::shared_ptr<OrderStorage> storage
                                            ) 
        {
            return  std::make_shared<OrderInternalData>(
                            std::move(parameters), 
                            std::move(instrument),
                            std::move(replaced_order),
                            std::move(create_time),                
                            std::move(storage)
                        );
        }


        coro::prepared_coro update(Fill &&up) {           
            std::scoped_lock _(mx);

            if (storage) {
                if (storage->check_fill_exists(up)) return {};
                auto wr = storage->write();
                storage->store_fill(wr, up);

                next_report.filled += up.quantity;
                next_report.turnover += up.contract.calc_turnover_pnl_currency(up.price, up.quantity);

                storage->store_filled(wr,key, {next_report.filled, next_report.turnover});
                wr.commit();
            } else {
                next_report.filled += up.quantity;
                next_report.turnover += up.contract.calc_turnover_pnl_currency(up.price, up.quantity);
            }
            next_report.fills.push_back(std::move(up));            
            return notify_lk();
        }

        coro::prepared_coro update(OrderStatus st) {
            if (storage && is_done_status(st)) {
                auto wr = storage->write();
                storage->close_order(wr, key);
            }
            std::scoped_lock _(mx);
            next_report.status = st;
            next_report.status_changed = true;
            done = is_done_status(st);
            return notify_lk();
        }

        coro::prepared_coro update(OrderRejectionReason st) {
            if (storage) {
                auto wr = storage->write();
                storage->close_order(wr, key);
            }
            std::scoped_lock _(mx);
            next_report.status = OrderStatus::rejected;
            next_report.rejection_reason = st;
            next_report.rejection_message = {};
            next_report.status_changed = true;
            done = true;
            return notify_lk();
        }
        coro::prepared_coro update(OrderRejectionWithText &&st) {
            if (storage) {
                auto wr = storage->write();
                storage->close_order(wr, key);
            }
            std::scoped_lock _(mx);
            next_report.status = OrderStatus::rejected;
            next_report.rejection_reason = st.reason;
            next_report.rejection_message = std::move(st.text);
            next_report.status_changed = true;
            done = true;
            return notify_lk();
        }

        coro::prepared_coro update(OrderOpenStatus &&st) {
            if (storage) {
                auto wr = storage->write();
                storage->open_order(wr, st.key, {st.id,parameters});
            }
            std::scoped_lock _(mx);
            id = std::move(st.id);
            key = st.key;
            next_report.status = OrderStatus::open;
            next_report.status_changed = true;
            done = false;
            return notify_lk();
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
        bool flush_updates(OrderReport &report) {
            std::scoped_lock _(mx);
            return flush_updates_lk(report);
        }
        ///Flush updates to report, set done_flag
        /**
            @param report reference to report
            @param done_flag is updtaed, when there is no updates. It is set to true, if order is done, or false is more updates expected
            @retval true any updates - done_flag is not changed
            @retval false no updates - done_flag set accordingly
        */
        bool flush_updates(OrderReport &report, bool &done_flag) {
            std::scoped_lock _(mx);
            bool x =  flush_updates_lk(report);
            if (!x) done_flag = true;            
            return x;
        }

        ///register awaiter
        /**
            @param report reference to report. Ensure reference valid during waiting
            @param promise reference to result promise. If not initialized, non-blocking operation is requested
            @return RegisterStatus status of operation
        */
        OrderReceiveReportStatus receive_report(OrderReport &report) {
            return receive_report_lk(report);
        }
        OrderReceiveReportStatus receive_report(OrderReport &report, awaitable<bool>::result &promise) {
            std::scoped_lock _(mx);
            return receive_report_lk(report, promise);
        }

        const OrderParameters &get_parameters() const {return parameters;}
        std::shared_ptr<ITradableInstrument> get_instrument() const {return instrument;}
        std::weak_ptr<OrderInternalData> get_replaced_order() const {return replaced_order;}
        std::chrono::system_clock::time_point get_create_time() const {return create_time;}
        void cancel() {if (cancel_fn) [[likely]] cancel_fn(this);}

        ///set cancel callback
        /**
            The provider must set cancel callback before the order is emited to the frontend, otherwise operation is not MT
        */
        void set_cancel_fn(CancelFn fn) {cancel_fn = std::move(fn);};
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
            this->next_report.filled = fst.filled;
            this->next_report.turnover = fst.turnover;
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
        Decimal get_remaining_quantity() const {return parameters.quantity - next_report.filled;}
        Decimal get_filled() const  {return  next_report.filled;}

    protected:
        ///current order parameters
        const OrderParameters parameters = {};
        ///associated instrument
        std::shared_ptr<ITradableInstrument> instrument = {};
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
    
        mutable std::mutex mx;

        OrderReport next_report;

        struct OrderAwaiting {
            OrderReport &report;
            awaitable<bool>::result result;
        };

        ///awaiting coroutine
        std::optional<OrderAwaiting> awaiting;
        ///order has been canceled internally - reserved for adapter
        std::atomic<bool> canceled = {};
        //order is done
        bool done = false;

        
        ///flush updates to report (internally locked by mutex)
        /**
            @param report target report
            @param reference to pointer to new OrderAdatapterData - used by trigger to point to a new order
            @retval true any updates applied
            @retval false no updated
         */
        bool flush_updates_lk(OrderReport &report) {
            if (next_report.fills.empty() && !next_report.status_changed) return false;

            //store some stats - because move over trivail types is UB
            auto filled = next_report.filled;
            auto turnover = next_report.turnover;
            
            //move
            report = std::move(next_report);
            
            //restore stats - hope, compiler will optimize out if not needed
            next_report.turnover = turnover;
            next_report.filled = filled;

            //status did not changed in new report
            next_report.status_changed = false;
            return true;
        }


        OrderReceiveReportStatus receive_report_lk(OrderReport &report) {
            //nobody should await yet
            if (awaiting) return OrderReceiveReportStatus::already_pending;

            //try to flush updates
            if (flush_updates_lk(report)) return OrderReceiveReportStatus::updates;

            if (done) return OrderReceiveReportStatus::done;
            return OrderReceiveReportStatus::need_await;
        }

        OrderReceiveReportStatus receive_report_lk(OrderReport &report, awaitable<bool>::result &promise) {
            auto res = receive_report_lk(report);
            if (res != OrderReceiveReportStatus::need_await) return res;

            //register promise and arguments
            awaiting.emplace(OrderAwaiting{report, std::move(promise)});
            //awaiting started
            return OrderReceiveReportStatus::awaiting;
                    
        }
        ///notify about update (locked)
        coro::prepared_coro notify_lk() {
            coro::prepared_coro out;
            //any awaiting
            if (awaiting.has_value()) {
                //flush updates
                if (flush_updates_lk(awaiting->report)) {
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

