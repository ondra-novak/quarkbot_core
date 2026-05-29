
#pragma once
#if 0
#include "basic_coro/awaitable_transform.hpp"
#include "../defs.hpp"
#include "../execution_worker.hpp"
#include "../types.hpp"
#include "../order_storage.hpp"
#include "../order_defs.hpp"
#include <chrono>
#include <deque>
#include <mutex>

namespace quarkbot {

class ITradableInstrument;


///internal state of order - implementation of Order
struct OrderInternalState{

     using Update = std::variant<Fill, OrderStatus, OrderRejectionReason, OrderRejectionWithText,  OrderOpenStatus>;

    ///original parameters -  adjusted
    const OrderParameters parameters = {};
    ///associated instrument
    std::shared_ptr<ITradableInstrument> instrument = {};
    ///order name
    std::string name = {};
    ///reference to replaced order
    std::weak_ptr<OrderInternalState> replaced_order = {};
    ///associated order storage
    std::shared_ptr<OrderStorage> storage;
    ///time, when this order has been created (or restored)
    std::chrono::system_clock::time_point create_time = {};
    ///internal order ID
    std::string id = {};
    ///adapter's generated unique record key - this can be used to store into database
    RecordKey key = {};
    ///filled amount (calculated locally)
    Decimal filled = {};
    ///order status
    OrderStatus status = OrderStatus::sent; 
    ///if order rejected, there is reject reason
    OrderRejectionReason reject_reason = {};
    ///if order rejected, there is message if any
    std::string rejection_message = {};
    ///all fills extracted from the queue;
    std::deque<Fill> fills;

    ///shared lock for queue
    std::mutex mx;
    ///queue of updates
    std::vector<Update> updates;
    ///awaiting coroutine
    ResultAndExecWorker<bool> awaiting = {};
    ///post co_await operation
    coro::awaitable_transform<awaitable<bool>, std::shared_ptr<OrderInternalState> > _awt_conv;
    ///order has been canceled internally - reserved for adapter
    std::atomic<bool> canceled = {};

    OrderInternalState(OrderParametersGen<Decimal> params, 
            PTradableInstrument instrument,
            std::string name,
            std::weak_ptr<OrderInternalState> replaced_order,
            std::shared_ptr<OrderStorage> storage,
            std::chrono::system_clock::time_point create_time
    ):parameters(std::move(params))
        ,instrument(std::move(instrument))
        ,name(std::move(name))
        ,replaced_order(std::move(replaced_order))
        ,storage(std::move(storage))
        ,create_time(create_time)
        {}
        
        
    void flush_updates() {
        //under lock
        std::scoped_lock _(mx);
        //to know where new fills starts
        auto new_fills_ofs = fills.size();

        std::vector<RecordKey> fill_dedup_helper;
        fill_dedup_helper.reserve(updates.size());

        //order was opened
        bool open_order = false;
        //order was closed
        bool close_order = false;
        //process updates
        for (auto &u: updates) {
            std::visit([&]<typename T>(T &v){
                if constexpr(std::is_same_v<T, OrderStatus>) {
                    status = v;
                    if (is_done_status(v)) {
                        close_order = true;
                    }
                } else if constexpr(std::is_same_v<T, OrderRejectionReason>) {
                    status = OrderStatus::rejected;
                    reject_reason = v;
                    close_order = true;
                } else if constexpr(std::is_same_v<T, OrderRejectionWithText>) {
                    status = OrderStatus::rejected;
                    reject_reason = v.reason;
                    rejection_message = v.text;
                    close_order = true;
                } else if constexpr(std::is_same_v<T, OrderOpenStatus>) {
                    id = v.id;
                    key = v.key;
                    status = OrderStatus::open;
                    open_order = true;
                } else if constexpr(std::is_same_v<T, Fill>) {
                    //local deduplication
                    auto found = std::find_if(fill_dedup_helper.begin(), fill_dedup_helper.end(), [&](const RecordKey &rc){
                        return rc ==  v.key;
                    });
                    if (found == fill_dedup_helper.end()) {
                        fill_dedup_helper.push_back(v.key);
                        fills.push_back(std::move(v));
                    }
                }
            }, u);                
        }
        //clear all updates - processed
        updates.clear();
        //if there is storage
        if (storage) {
            //find how many fills added
            auto sz = fills.size();
            //we can store order only if it was open or closed, not both or neither
            bool open_or_close = open_order != close_order;
            //so test, whether initiate store
            if (open_or_close ||new_fills_ofs < sz) {
                //create transaction
                auto tx = storage->write();
                //we storing open or close
                if (open_or_close) {
                    //store what needed
                    if (open_order) storage->open_order(tx, key, {id, name, parameters});
                    else if (close_order) storage->close_order(tx, key);
                }
                //store all fills
                bool filled_changed = false;
                auto new_end = std::remove_if(fills.begin()+ static_cast<std::ptrdiff_t>(new_fills_ofs), fills.end(),
                    [&](const Fill &f) {
                        //global deduplication
                        if (storage->check_fill_exists(f)) return true;
                        storage->store_fill(tx, f);
                        filled = filled + f.amount;
                        filled_changed = true;
                        return false;
                    });
                fills.erase(new_end, fills.end());
                if (filled_changed && !close_order) {
                    storage->store_filled(tx,key,filled);
                }
                tx->commit();
            } else {
                for (auto &x: fills) {
                    filled = filled + x.amount;
                }
            }
        }
        //now order is ready for frontend
    }
    void update(Update &&u) {
        std::scoped_lock _(mx);
        updates.push_back(std::move(u));
        if (awaiting) {
            awaiting(true);
        }
    }

    static awaitable<bool> next_event(std::shared_ptr<OrderInternalState> me) {         
        auto inner = [](std::shared_ptr<OrderInternalState> me)->awaitable<bool>{
            std::scoped_lock _(me->mx);
            if (!me->updates.empty()) return true;                
            if (is_done_status(me->status)) return false;                                    
            return [me = std::move(me)](auto promise) -> coro::prepared_coro {
                coro::prepared_coro out;
                std::scoped_lock _(me->mx);
                if (!me->updates.empty()) out = promise(true);
                else if (is_done_status(me->status)) out = promise(false);
                else me->awaiting = std::move(promise);
                return out;
            };
        };
        return me->_awt_conv(inner(me),[me](bool v){
            if (v) me->flush_updates();                                    
            return v;
        });
    }    


    ///determines whether update carries done status (is_done_status)
    static bool is_done_update(const OrderInternalState::Update &up) {        
        if (std::holds_alternative<OrderStatus>(up)) return is_done_status(std::get<OrderStatus>(up));
        if (std::holds_alternative<OrderRejectionReason>(up)    //rejection
            || std::holds_alternative<OrderRejectionWithText>(up)) return true; //rejection
        return false;
    }

};
    



}
#endif
    