#pragma once

#include "basic_coro/prepared_coro.hpp"
#include "quarkbot/abstract/iorder.hpp"
#include "quarkbot/defs.hpp"
#include "quarkbot/order_defs.hpp"
#include "order_internal_defs.hpp"
#include "quarkbot/types.hpp"
namespace quarkbot {




    static constexpr OrderParameters empty_order_parameters = {};

    

    class OrderInternalData;
    class ITradableInstrument;
    using POrderData = std::shared_ptr<OrderInternalData>;
    using OrderData = OrderInternalData;

    constexpr OrderStatus rejection_reason_2_status(OrderRejectionReason rej) {    
        return (rej == OrderRejectionReason::expired || rej == OrderRejectionReason::post_only_taker)
            ?OrderStatus::canceled:OrderStatus::rejected;
    }


    class OrderInternalData: public IOrder {
    public:
        //update definition

        using Update = OrderStatusUpdate;
        
        
        OrderInternalData(
            OrderParameters parameters,
            PTradableInstrument instrument,
            std::weak_ptr<IOrder> replaced_order,            
            std::chrono::system_clock::time_point create_time)
                :parameters(std::move(parameters))
                ,instrument(std::move(instrument))
                ,replaced_order(std::move(replaced_order))
                ,create_time(std::move(create_time))  {}

        OrderInternalData(const OrderInternalData &) = delete;
        OrderInternalData &operator=(const OrderInternalData &) = delete;                

        coro::prepared_coro update(Fill &&up) {           
            std::scoped_lock _(mx);

            next_report.fills.push_back(std::move(up));            
            return notify_lk();
        }

        coro::prepared_coro update(OrderStatus st) {
            std::scoped_lock _(mx);
            next_report.status = st;
            next_report.status_changed = true;
            done = is_done_status(st);
            return notify_lk();
        }

        coro::prepared_coro update(OrderRejectionReason st) {
            std::scoped_lock _(mx);
            next_report.status = OrderStatus::rejected;
            next_report.rejection_reason = st;
            next_report.rejection_message = {};
            next_report.status_changed = true;
            done = true;
            return notify_lk();
        }
        coro::prepared_coro update(OrderRejectionWithText &&st) {
            std::scoped_lock _(mx);
            next_report.status = OrderStatus::rejected;
            next_report.rejection_reason = st.reason;
            next_report.rejection_message = std::move(st.text);
            next_report.status_changed = true;
            done = true;
            return notify_lk();
        }

        coro::prepared_coro update(OrderOpenStatus &&st) {
            std::scoped_lock _(mx);
            id = std::move(st.id);
            key = st.key;
            next_report.status = OrderStatus::open;
            next_report.status_changed = true;
            done = false;
            return notify_lk();
        }
        coro::prepared_coro update(const OrderFillStats &st) {
            next_report.fill_stats = st;
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
        IOrder::RcvStatus receive_report(OrderReport &report) override {
            return receive_report_lk(report);
        }
        IOrder::RcvStatus receive_report(OrderReport &report, awaitable<bool>::result &promise) override {
            std::scoped_lock _(mx);
            return receive_report_lk(report, promise);
        }

        const OrderParameters &get_parameters() const override {return parameters;}
        const PTradableInstrument & get_instrument() const override {return instrument;}
        std::weak_ptr<IOrder> get_replaced_order() const override {return replaced_order;}
        virtual std::chrono::system_clock::time_point get_creation_time() const override {return create_time;}        

        
        std::string_view get_id() const override{
            std::scoped_lock _(mx);
            return id;  //id will not change when it is set, so it is safe to return view which is either empty or valid
        }
        void set_id(std::string id) {this->id = std::move(id);}
        Decimal get_remaining_quantity() const {return parameters.quantity - next_report.fill_stats.filled;}
        Decimal get_filled() const  {return  next_report.fill_stats.filled;}
        const OrderFillStats &get_fill_stats() const {return next_report.fill_stats;}

        bool mark_canceled() {
            return !canceled.exchange(true, std::memory_order_relaxed);
        }        

        const RecordKey &get_key() const {return key;}

    protected:
        ///current order parameters
        const OrderParameters parameters = {};
        ///associated instrument
        std::shared_ptr<ITradableInstrument> instrument = {};
        ///reference to replaced order
        std::weak_ptr<IOrder> replaced_order = {};
        ///time, when this order has been created (or restored)
        std::chrono::system_clock::time_point create_time = {};
        ///adapter's generated unique record key - this can be used to store into database
        RecordKey key = {};
        ///internal order ID
        std::string id = {};
        ///order has been canceled internally - reserved for adapter
        std::atomic<bool> canceled = {};
    
        mutable std::mutex mx;

        OrderReport next_report;

        struct OrderAwaiting {
            OrderReport &report;
            awaitable<bool>::result result;
        };

        ///awaiting coroutine
        std::optional<OrderAwaiting> awaiting;
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
            auto filled = next_report.fill_stats;
            
            //move
            report = std::move(next_report);
            
            //restore stats - hope, compiler will optimize out if not needed
            next_report.fill_stats = std::move(filled);

            //status did not changed in new report
            next_report.status_changed = false;
            return true;
        }


        IOrder::RcvStatus receive_report_lk(OrderReport &report) {
            //nobody should await yet
            if (awaiting) return IOrder::RcvStatus::already_pending;

            //try to flush updates
            if (flush_updates_lk(report)) return IOrder::RcvStatus::updates;

            if (done) return IOrder::RcvStatus::done;
            return IOrder::RcvStatus::need_await;
        }

        IOrder::RcvStatus receive_report_lk(OrderReport &report, awaitable<bool>::result &promise) {
            auto res = receive_report_lk(report);
            if (res != IOrder::RcvStatus::need_await) return res;

            //register promise and arguments
            awaiting.emplace(OrderAwaiting{report, std::move(promise)});
            //awaiting started
            return IOrder::RcvStatus::awaiting;
                    
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


    template<std::invocable<IOrder *> _CancelCB>
    class OrderWithCancelCallback: public OrderInternalData {
    public:
        OrderWithCancelCallback(OrderParameters parameters,
                                PTradableInstrument instrument,
                                std::weak_ptr<IOrder> replaced_order,            
                                std::chrono::system_clock::time_point create_time,
                                _CancelCB cb
                            )
            :OrderInternalData(std::move(parameters),
                               std::move(instrument),
                               std::move(replaced_order),
                               std::move(create_time))
            ,_cancelCB(std::move(cb)) {}
            ~OrderWithCancelCallback() {
                if (!this->parameters.keep_alive && !this->done) {
                    OrderWithCancelCallback::cancel();
                }
            }

            virtual void cancel() override {
                if (mark_canceled()) _cancelCB(this);
            }


    protected:
        _CancelCB _cancelCB;
    };

}

