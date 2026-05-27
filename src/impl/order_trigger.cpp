#include "order_trigger.hpp"
#include "ifc/order_defs.hpp"
#include "ifc/streaming.hpp"
#include "ifc/tradable_instrument.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/types.hpp"
#include <exception>
#include <mutex>
#include <utility>
#include <variant>

namespace quarkbot {

   template<typename Fn>
    auto OrderTrigger::update_state(const Order &ord, Fn &&fn){
        std::scoped_lock _(_mx);
        auto &st = _order_map[ord];
        return fn(st);
    }


    void OrderTrigger::register_order(Order ord){
        update_state(ord, [](auto &){/* empty, just initialize state*/});
        _worker->run(monitor_order(shared_from_this(), std::move(ord)));
    }
 
    void OrderTrigger::cancel_order(Order ord){
        //lock internal state
        std::scoped_lock _(_mx);
        //find order
        auto iter = _order_map.find(ord);        
        if (iter == _order_map.end()) return;//nothing to cancel
        //determine state
        auto &st = iter->second;
        if (st.canceled) return ; //already canceled
        //if still on trigger (we have stream)
        if (std::holds_alternative<Stream *>(st.phase)){
            //close stream
            auto &s = std::get<Stream*>(st.phase);        
            s->close();
        //if order has been already placed
        } else if (std::holds_alternative<Order *>(st.phase)){
            auto o = std::get<Order*>(st.phase);
            //cancel order
            o->cancel();
        } 
        //mark order canceled                
        st.canceled = true;
    }

    bool OrderTrigger::cancel_order_for_replace(const Order &ord) {
        std::scoped_lock _(_mx);
        auto iter = _order_map.find(ord);
        if (iter == _order_map.end()) return false;
        auto &st = iter->second;
        if (st.canceled) return false;
        if (std::holds_alternative<Stream*>(st.phase)){            
            auto &s = std::get<Stream *>(st.phase);
            s->close();
        } else {
            return false;
        }
        st.canceled = true;
        return true;
    }


    void OrderTrigger::unreg_order(const Order &ord) {
        std::scoped_lock _(_mx);
        _order_map.erase(ord);
    }

    StrategyFragment OrderTrigger::monitor_order(std::shared_ptr<OrderTrigger> me, Order order){
        try {
            //get parameters
            const auto &params = order.get_parameters();        
            //trigger price
            Decimal trigger_price;
            //triger side (-1 price must be below, 1 price must be above)
            int trigger_sign;

            //validate and prepare limit order
            if (params.type == OrderType::limit){
                if (params.limit_price <= 0_dec) {
                    order.update_order(Order::RejectionWithText{OrderRejectionReason::invalid_params, "missing limit price"});
                    me->unreg_order(order);
                    co_return;
                }     
                trigger_price = params.limit_price;
                trigger_sign = -static_cast<int>(params.side);
            //validate and prepare stop like orders
            } else if (params.type == OrderType::stop || params.type == OrderType::stoplimit || params.type == OrderType::alert) {
                if (params.stop_price <= 0_dec) {
                    order.update_order(Order::RejectionWithText{OrderRejectionReason::invalid_params, "missing stop price"});
                    me->unreg_order(order);
                    co_return;
                }     
                if (params.type == OrderType::stoplimit && params.limit_price <= 0_dec){
                    order.update_order(Order::RejectionWithText{OrderRejectionReason::invalid_params, "missing limit price"});
                    me->unreg_order(order);
                    co_return;
                }

                trigger_price = params.stop_price;
                trigger_sign = static_cast<int>(params.side);
            } else {
                //report error - not supported
                order.update_order(Order::RejectionWithText{OrderRejectionReason::unsupported, "unsupported order for local trigger"});
                me->unreg_order(order);
                co_return ;
            }

            //handle replace now
            auto repl = order.get_replaced_order();
            if (repl) {
                auto replst = repl->get_status();
                if (replst == OrderStatus::sent){
                    order.update_order(Order::RejectionWithText{OrderRejectionReason::invalid_replace, "not ready for replace"});
                    me->unreg_order(order);
                    co_return ;
                }
                if (!me->cancel_order_for_replace(*repl)) {
                    order.update_order(Order::RejectionWithText{OrderRejectionReason::order_not_found, "order cannot be replaced"});
                    me->unreg_order(order);
                    co_return ;
                }
                //we no longer need replaced order, so reset it
                repl.reset();
            }

            //retrieve instrument for subscription
            auto instr = order.get_instrument();

            //subscribe trades
            Stream ev= instr->get_instrument()->subscribe<Trade>();
            //create and update state with reference to event stream
            auto canceled = me->update_state(order, [&](State &st){
                st.phase = &ev;           
                return st.canceled;
            });

            if (canceled) {
                order.update_order(OrderStatus::canceled);          
                me->unreg_order(order);
                co_return;
            }

            //update order status
            order.update_order(OrderStatus::pending_trigger);

            //monitor prices in cycle
            Trade tev;
            while (true) {
                //read trade event
                bool r = co_await ev.next(tev);
                //when stream is closed 
                if (!r) {
                    //mark order canceled
                    order.update_order(OrderStatus::canceled);
                    //and exit
                    me->unreg_order(order);
                    co_return;
                }
                //check whether trigger price has been reached
                if ((tev.price - trigger_price) * trigger_sign>=0) {
                    //exit cycle
                    break;
                }
            }

            //now we place new order
            //prepare parameters
            OrderRequest ordreq;        

            switch (params.type) {            
                //triggered limit is sent as limit
                case OrderType::limit: ordreq.type = OrderType::limit; ordreq.limit_price = params.limit_price; break;
                //triggered stop is sent as market
                case OrderType::stop: ordreq.type = OrderType::market; break;
                //triggered stoplimit is sent as limit
                case OrderType::stoplimit: ordreq.type = OrderType::limit; ordreq.limit_price = params.limit_price; break;
                //alert is market filled
                case OrderType::alert:
                    order.update_order(OrderStatus::filled);
                    me->unreg_order(order);
                    co_return;
                default:
                    //report error
                    order.update_order(Order::RejectionWithText{OrderRejectionReason::invalid_params, "Failed to convert parameters of the order"});
                    me->unreg_order(order);
                    co_return;
            }
            //copy other arguments
            ordreq.leverage = params.leverage;
            ordreq.hedge = params.hedge;
            ordreq.quantity = params.quantity;
            ordreq.reason_override = params.reason_override;
            ordreq.side = params.side;
            ordreq.time_in_force = params.time_in_force;
            ordreq.reduce_only = params.reduce_only;        
            
            //place the order
            Order new_order = instr->place_order(ordreq, order.get_name());

            //mark original order sent
            OrderStatus prev_status = OrderStatus::sent;        
            order.update_order(prev_status);

            //update state in order map, detect canceled state atomically
            canceled = me->update_state(order, [&](State &st){
                st.phase = &new_order;
                return st.canceled;
            });

            //if canceled, cancel the order
            if (canceled) new_order.cancel();

            //process all events
            while (co_await new_order.next()) {
                auto f = new_order.read_fill();
                //forward fills
                if (f) order.update_order(std::move(f).value());
                auto st = order.get_status();
                //if status changed
                if (st != prev_status) {
                    //handle open status
                    if (st == OrderStatus::open) {
                        order.update_order(Order::OpenStatus{order.get_id(), order.get_record_key()});    
                    //handle rejected status
                    } else if (st == OrderStatus::rejected){
                        order.update_order(Order::RejectionWithText{order.get_reject_reason(), order.get_rejection_message()});    
                    } else {
                        //forward any other status
                        order.update_order(st);
                    }
                    prev_status = st;
                }
            }
            //if done, unreg order
            me->unreg_order(order);
            //exit
            co_return;        
        } catch (const std::exception &e) {
            order.update_order(Order::RejectionWithText{OrderRejectionReason::internal_error, e.what()});
            me->unreg_order(order);
            co_return;
        }

    }

}