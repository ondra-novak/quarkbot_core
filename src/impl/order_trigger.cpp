#include "order_trigger.hpp"
#include "ifc/abstract/orderdata.hpp"
#include "ifc/order.hpp"
#include "ifc/order_defs.hpp"
#include "ifc/tradable_instrument.hpp"
#include <memory>
#include <mutex>
#include <utility>
#include <variant>

namespace quarkbot {

    std::shared_ptr<OrderTrigger> OrderTrigger::create(ExecutionWorker worker) {
        return std::make_shared<OrderTrigger>(std::move(worker));
    }


   template<typename Fn>
    auto OrderTrigger::update_state(const POrderAData &ord, Fn &&fn){
        std::scoped_lock _(_mx);
        auto &st = _order_map[ord];
        return fn(st);
    }

   Order OrderTrigger::place_order(PTradableInstrument instrument,            
            const OrderParameters &params,
            std::shared_ptr<OrderStrategyData> order_to_replace, 
            std::string_view name, std::size_t ) 
    {
        auto strdata = OrderStrategyData::create();
        auto adata = OrderAdapterData::create(strdata, params, instrument, 
            std::string(name), order_to_replace,
             _worker.now(),
             [me = shared_from_this()](OrderStrategyData &what) {
                    me->cancel_order(what.adapter_data);
                },{});

        update_state(adata, [](auto &){/* empty, just initialize state*/});
       _worker.run(monitor_order(shared_from_this(), std::move(adata)));
        return Order(strdata);
        
    }

 
    bool OrderTrigger::cancel_order(POrderAData ord){
        //lock internal state
        std::scoped_lock _(_mx);
        //find order
        auto iter = _order_map.find(ord);        
        if (iter == _order_map.end()) return false;//nothing to cancel
        //determine state
        auto &st = iter->second;
        if (st.canceled) return false; //already canceled
        //if still on trigger (we have stream)
        if (st.stream) {
            st.stream->close();
        }
        st.canceled = true;
        return true;
    }


    void OrderTrigger::unreg_order(const POrderAData &ord) {
        std::scoped_lock _(_mx);
        _order_map.erase(ord);
    }

    StrategyFragment OrderTrigger::monitor_order(std::shared_ptr<OrderTrigger> me, POrderAData order){
        try {

            //get parameters
            const auto &params = order->parameters;        
            //trigger price
            Decimal trigger_price;
            //triger side (-1 price must be below, 1 price must be above)
            int trigger_sign;

            //validate and prepare limit order
            if (params.type == OrderType::limit){
                if (params.limit_price <= 0_dec) {
                    order->update(OrderRejectionWithText{OrderRejectionReason::invalid_params, "missing limit price"});
                    me->unreg_order(order);
                    co_return;
                }     
                trigger_price = params.limit_price;
                trigger_sign = -static_cast<int>(params.side);
            //validate and prepare stop like orders
            } else if (params.type == OrderType::stop || params.type == OrderType::stoplimit || params.type == OrderType::alert) {
                if (params.stop_price <= 0_dec) {
                    order->update(OrderRejectionWithText{OrderRejectionReason::invalid_params, "missing stop price"});
                    me->unreg_order(order);
                    co_return;
                }     
                if (params.type == OrderType::stoplimit && params.limit_price <= 0_dec){
                    order->update(OrderRejectionWithText{OrderRejectionReason::invalid_params, "missing limit price"});
                    me->unreg_order(order);
                    co_return;
                }

                trigger_price = params.stop_price;
                trigger_sign = static_cast<int>(params.side);
            } else {
                //report error - not supported
                order->update(OrderRejectionWithText{OrderRejectionReason::unsupported, "unsupported order for local trigger"});
                me->unreg_order(order);
                co_return ;
            }

            //handle replace now
            auto repl = order->replaced_order.lock();            
            if (repl) {
                auto replst = repl->status;
                if (replst == OrderStatus::sent){
                    order->update(OrderRejectionWithText{OrderRejectionReason::invalid_replace, "not ready for replace"});
                    me->unreg_order(order);
                    co_return ;
                }
                if (!me->cancel_order(repl->adapter_data)) {
                    order->update(OrderRejectionWithText{OrderRejectionReason::order_not_found, "order cannot be replaced"});
                    me->unreg_order(order);
                    co_return ;
                }
                //we no longer need replaced order, so reset it
                repl.reset();
            }

            //retrieve instrument for subscription
            TradableInstrument instr( order->instrument);

            //subscribe trades
            Stream ev= instr.get_instrument().subscribe<Trade>();
            //create and update state with reference to event stream
            auto canceled = me->update_state(order, [&](State &st){
                st.stream = &ev;           
                return st.canceled;
            });

            if (canceled) {
                order->update(OrderStatus::canceled);          
                me->unreg_order(order);
                co_return;
            }

            //update order status
            order->update(OrderStatus::pending_trigger);

            //monitor prices in cycle
            Trade tev;
            while (true) {
                //read trade event
                bool r = co_await ev.receive(tev);
                //when stream is closed 
                if (!r) {
                    //mark order canceled
                    order->update(OrderStatus::canceled);
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
                    order->update(OrderStatus::filled);
                    me->unreg_order(order);
                    co_return;
                default:
                    //report error
                    order->update(OrderRejectionWithText{OrderRejectionReason::invalid_params, "Failed to convert parameters of the order"});
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
            Order new_order = instr.place_order(ordreq, order->name);

            //mark original order sent
            OrderStatus prev_status = OrderStatus::sent;        
            order->update(prev_status);

            //update state in order map, detect canceled state atomically
            canceled = me->update_state(order, [&](State &st){
                return st.canceled;
            });

            //if canceled, cancel the order
            if (canceled) new_order.cancel();

            //redirect adapter state to old order - so it will receive fills and updates
            order->update(new_order.get_handle()->adapter_data);
            //new order is still attached to its adapter data
            //we need to keep them alive 
            new_order.keep_alive();
            //new order will be destroyed and detaches itself from adapter data
        } catch (std::exception &e) {
            order->update(OrderRejectionWithText{OrderRejectionReason::internal_error, e.what()});
            me->unreg_order(order);
        }

    }

}