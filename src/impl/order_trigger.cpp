#include "order_trigger.hpp"
#include "ifc/abstract/orderdata.hpp"
#include "ifc/order.hpp"
#include "ifc/order_defs.hpp"
#include "ifc/tradable_instrument.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace quarkbot {

    class TrigOrder: public OrderInternalData {
    public:
        using OrderInternalData::OrderInternalData;

        void cancel_request();
        static StrategyFragment monitor_order(std::shared_ptr<TrigOrder> order, std::function<Order()> place_request);

        EventStream<Trade> *ev = nullptr;
        std::shared_ptr<OrderInternalData> placed_order = {};

        bool cancel_for_replace();
        bool replaced = false;        
        bool set_event(EventStream<Trade> *ev);
        bool set_new_order(std::shared_ptr<OrderInternalData> new_order);
        bool was_replaced() const;
    };

    void TrigOrder::cancel_request() {
        std::scoped_lock _(mx);        
        canceled.store(true);
        if (ev) ev->close();
        if (placed_order) placed_order->cancel();
    }

    bool TrigOrder::cancel_for_replace() {
        std::scoped_lock _(mx);
        if (canceled.exchange(true)) return false;
        if (placed_order) return false;
        if (ev) ev->close();
        replaced = true;
        return true;
    }

    bool TrigOrder::set_event(EventStream<Trade> *ev) {
        std::scoped_lock _(mx);
        if (canceled.load(std::memory_order_relaxed)) return false;
        this->ev = ev;
        return true;
    }

    bool TrigOrder::was_replaced() const {
        std::scoped_lock _(mx);
        return replaced;
    }
    
    bool TrigOrder::set_new_order(std::shared_ptr<OrderInternalData> new_order) {
        std::scoped_lock _(mx);
        ev = nullptr;
        if (canceled.exchange(true)) return false;
        placed_order = std::move(new_order);        
        return true;
    }


    std::shared_ptr<OrderInternalData> OrderTrigger::place_order(PTradableInstrument instrument, 
                    const OrderParameters &trig_params, //params reported by order while trigger phase
                    std::shared_ptr<OrderInternalData> order_to_replace, 
                    std::string_view name, std::function<Order()> place_request) {
        
        auto nword = std::make_shared<TrigOrder>(trig_params,instrument,
                std::string(name), order_to_replace, _worker.now(),                 
                std::shared_ptr<OrderStorage>{});
        nword->set_cancel_fn([](OrderInternalData *me){
            static_cast<TrigOrder *>(me)->cancel_request();
        });
        
        _worker.run(TrigOrder::monitor_order(nword, place_request));
        return nword;
    }

    std::shared_ptr<OrderInternalData> OrderTrigger::place_order(PTradableInstrument instrument, 
                const OrderParameters &trig_params, //params reported by order while trigger phase                    
                std::shared_ptr<OrderInternalData> order_to_replace, 
                std::string_view name) {
        if (trig_params.type == OrderType::alert) {
            return place_order(instrument, trig_params, order_to_replace, name, {});
        } else {
            OrderRequest req;
            std::string sname (name);
            bool b = convert_params_to_request(trig_params, req);
            if (!b) {
                auto out =  OrderInternalData::create(trig_params, instrument, std::move(sname), 
                    order_to_replace, _worker.now(), {});
                out->update(OrderRejectionWithText{OrderRejectionReason::invalid_params, "Not supported by local trigger"});
                return out;
            } else {
                return place_order(instrument, trig_params, order_to_replace, sname,[=]{
                    return TradableInstrument(instrument).place_order(req, sname);
                });                
            }
        }
    }


    StrategyFragment TrigOrder::monitor_order(std::shared_ptr<TrigOrder> order, std::function<Order()> place_request) {
        try {


            //------------------ VALIDATION --------------------------------------
            //get parameters
            const auto &params = order->get_parameters();        
            //trigger price
            Decimal trigger_price;
            //triger side (-1 price must be below, 1 price must be above)
            int trigger_sign;

            //validate and prepare limit order
            if (params.type == OrderType::limit){
                if (params.limit_price <= 0_dec) {
                    order->update(OrderRejectionWithText{OrderRejectionReason::invalid_params, "missing limit price"});                    
                    co_return;
                }     
                trigger_price = params.limit_price;
                trigger_sign = -static_cast<int>(params.side);
            //validate and prepare stop like orders
            } else if (params.type == OrderType::stop || params.type == OrderType::stoplimit || params.type == OrderType::alert) {
                if (params.stop_price <= 0_dec) {
                    order->update(OrderRejectionWithText{OrderRejectionReason::invalid_params, "missing stop price"});
                    co_return;
                }     
                if (params.type == OrderType::stoplimit && params.limit_price <= 0_dec){
                    order->update(OrderRejectionWithText{OrderRejectionReason::invalid_params, "missing limit price"});
                    co_return;
                }

                trigger_price = params.stop_price;
                trigger_sign = static_cast<int>(params.side);
            } else {
                //report error - not supported
                order->update(OrderRejectionWithText{OrderRejectionReason::unsupported, "unsupported order for local trigger"});
                co_return ;
            }

            //handle replace now
            auto repl = order->get_replaced_order().lock();            
            if (repl) {
                auto repl_trig = std::dynamic_pointer_cast<TrigOrder>(repl);
                if (!repl_trig) {
                    order->update(OrderRejectionReason::invalid_replace);
                    co_return;
                }

                if (!repl_trig->cancel_for_replace()) {
                    order->update(OrderRejectionReason::order_not_found);
                    co_return ;

                }
                //we no longer need replaced order, so reset it
                repl.reset();
                repl_trig.reset();
            }

            // ----------------------- MONITOR PRICE ----------------------
            auto ev = TradableInstrument(order->instrument).get_instrument().subscribe<Trade>();
            if (!order->set_event(&ev)) {
                order->update(OrderStatus::canceled);
                co_return;
            }
            order->update(OrderStatus::pending_trigger);
            
            //monitor prices in cycle
            Trade tev;
            while (true) {
                //read trade event
                bool r = co_await ev.receive(tev);
                //when stream is closed 
                if (!r) {
                    //mark order canceled
                    order->update(order->was_replaced()?OrderStatus::replaced:OrderStatus::canceled);
                    co_return;
                }
                //check whether trigger price has been reached
                if ((tev.price - trigger_price) * trigger_sign>=0) {
                    //exit cycle
                    break;
                }
            }
            //----------------- Handle Alert ----------------------------

            if (params.type == OrderType::alert) {
                order->update(OrderStatus::filled);
                co_return;
            }

            //----------------- PLACE NEW ORDER ----------------------------

            auto placed = place_request();
            

            if (!order->set_new_order(placed.get_handle())) {
                placed.cancel();                
            }

            order->update(OrderStatus::sent);            
            //redirect all updates to triggered order
            placed.get_handle()->redirect_updates(order.get());
            co_return;

        } catch (std::exception &e) {
            order->update(OrderRejectionWithText{OrderRejectionReason::internal_error, e.what()});
        }
    }


    bool OrderTrigger::convert_params_to_request(const OrderParameters &params, OrderRequest &ordreq) {
            switch (params.type) {            
                //triggered limit is sent as limit
                case OrderType::limit: ordreq.type = OrderType::limit; ordreq.limit_price = params.limit_price; break;
                //triggered stop is sent as market
                case OrderType::stop: ordreq.type = OrderType::market; break;
                //triggered stoplimit is sent as limit
                case OrderType::stoplimit: ordreq.type = OrderType::limit; ordreq.limit_price = params.limit_price; break;
                default: return false;
            }
            //copy other arguments
            ordreq.leverage = params.leverage;
            ordreq.hedge = params.hedge;
            ordreq.quantity = params.quantity;
            ordreq.reason_override = params.reason_override;
            ordreq.side = params.side;
            ordreq.time_in_force = params.time_in_force;
            ordreq.reduce_only = params.reduce_only;    
            ordreq.local_trigger = false;    
            return true;

    }
}