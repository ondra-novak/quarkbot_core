#pragma once

#include "basic_coro/awaitable_transform.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "execution_worker.hpp"
#include "order_defs.hpp"
#include "order_storage.hpp"
#include "types.hpp"
#include "defs.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <variant>
#include "utils/decimal.hpp"



namespace quarkbot {

class Order {
public:

    struct RejectionWithText {
        OrderRejectionReason reason;
        std::string text;
    };

    struct OpenStatus {
        std::string id;
        RecordKey key;
    };

    using Update = std::variant<Fill, OrderStatus, OrderRejectionReason, RejectionWithText,  OpenStatus>;

    struct State{
        ///original parameters -  adjusted
        const OrderParameters parameters = {};
        ///associated instrument
        PTradableInstrument instrument = {};
        ///order name
        std::string name = {};
        ///reference to replaced order
        std::weak_ptr<State> replaced_order = {};
        ///associated order storage
        std::shared_ptr<OrderStorage> storage;
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
        std::vector<Fill> fills;

        ///shared lock for queue
        std::mutex mx;
        ///queue of updates
        std::vector<Update> updates;
        ///awaiting coroutine
        ResultAndExecWorker<bool> awaiting = {};
        ///post co_await operation
        coro::awaitable_transform<awaitable<bool>, std::shared_ptr<State> > _awt_conv;
        

        State(OrderParametersGen<Decimal> params, 
              PTradableInstrument instrument,
              std::string name,
              std::weak_ptr<State> replaced_order
        ):parameters(std::move(params))
         ,instrument(std::move(instrument))
         ,name(std::move(name))
         ,replaced_order(std::move(replaced_order))
         {}
         
         
        void flush_updates() {
            //under lock
            std::scoped_lock _(mx);
            //to know where new fills starts
            auto new_fills_ofs = fills.size();
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
                    } else if constexpr(std::is_same_v<T, RejectionWithText>) {
                        status = OrderStatus::rejected;
                        reject_reason = v.reason;
                        rejection_message = v.text;
                        close_order = true;
                    } else if constexpr(std::is_same_v<T, OpenStatus>) {
                        id = v.id;
                        key = v.key;
                        status = OrderStatus::open;
                        open_order = true;
                    } else if constexpr(std::is_same_v<T, Fill>) {
                        fills.push_back(std::move(v));
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
                        if (open_order) storage->open_order(tx, key, id, parameters);
                        else if (close_order) storage->close_order(tx, key);
                    }
                    //store all fills
                    for (std::size_t p = new_fills_ofs; p < sz; ++p){
                        storage->store_fill(tx, fills[p]);
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


        static awaitable<bool> next_event(std::shared_ptr<State> me) {         
            auto inner = [](std::shared_ptr<State> me)->awaitable<bool>{
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
    };
        
    Order(std::shared_ptr<State> st):_state(std::move(st)) {}

    Order(OrderParametersGen<Decimal> params, 
        PTradableInstrument instrument,
        std::string name,
        Order replaced_order
    ):_state(std::make_shared<State>(std::move(params),std::move(instrument), std::move(name), replaced_order._state)) {}

    Order(OrderParametersGen<Decimal> params, 
        PTradableInstrument instrument,
        std::string name
    ):_state(std::make_shared<State>(std::move(params),std::move(instrument), std::move(name),  std::weak_ptr<State>{})) {}


    ///Wait for next event (awaitable)
    /**
    @retval true a state of order has been changed, or there are unprocessed fill. State update is applied to order instance.
    @retval false order is done
    @note If co_awaited, the function will suspend until there is new event (fill or status update) for this order. 
    @note only one co_await can be active at the same time, otherwise behavior is undefined. 
           You must co_await from execution thread, otherwise exception is thrown. However it is 
           possible to co_await in different execution thread than order has been created.

    @note new order state is populated only during this operation. Even if you know, that new state of order is avaialble, you 
    still need to call this function to correctly update internals of the order

        
    */
    awaitable<bool> next() {
        return State::next_event(_state);
    }

    ///Get all fills
    /**
        @return fills. Note you have write access to this array. You can process fills and clear the array to capture new fills
     */
    std::vector<Fill> &get_fills() {return _state->fills;}
    ///Get all fills
    const std::vector<Fill> &get_fills() const {return _state->fills;}


    ///get order parameters
    const OrderParametersGen<Decimal> &get_parameters() const {return _state->parameters;}
    ///get instrument
    PTradableInstrument get_instrument() const {return _state->instrument;}
    ///get order name
    const std::string &get_name() const {return _state->name;}    
    ///retrieve order instance which has been replaced
    /**
        @return optional containing order instance. Note that to return
            valid order instance, it must still exists somewhere in the
            system. Once the last reference is removed, the previous order is no longer
            available and function returns nullopt
    */
    std::optional<Order> get_replaced_order() const {
        auto lk = _state->replaced_order.lock();
        std::optional<Order> out;
        if (lk) out.emplace(lk);
        return out;
    }
    ///return internal id
    const std::string &get_id() const {return _state->id;}
    ///return filled amount
    Decimal get_filled() const {return _state->filled;}
    ///return get order status
    OrderStatus get_status() const {return _state->status;}
    ///get reason for rejection
    OrderRejectionReason get_reject_reason() const {return _state->reject_reason;}
    ///get rejection message
    const std::string &get_rejection_message() const {return _state->rejection_message;}    

    ///update order status
    /**
    */
    void update_order(Update &&update) {
        _state->update(std::move(update));
    }


    void cancel();
    Decimal get_turnover(Decimal price, Decimal filled = {}) const;
    bool done() const {return is_done_status(get_status());}

    
    bool operator==(const Order &) const = default;
    
    ///create hash (for unordered map)
    std::uint64_t hash() const {
        std::hash<std::shared_ptr<State> > hasher;
        return hasher(_state);
    }

protected:

    std::shared_ptr<State> _state = {};
};

using SerializedOrder = std::string;


}
    