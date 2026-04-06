#include "simexecutor.hpp"
#include "impl/simexchange.hpp"
#include "siminstrument.hpp"    
#include "ifc/defs.hpp"
#include "ifc/order.hpp"
#include "ifc/types.hpp"
#include "utils/decimal.hpp"
#include "utils/random_string.hpp"
#include "simtradableinstrument.hpp"
#include <algorithm>
#include <memory>

namespace quarkbot {

    SimExecutor::PSimInstrument SimExecutor::extract_instrument(const Order &ord) {
        auto instr = ord.get_instrument();
        auto minstr = instr->get_instrument();
        return std::dynamic_pointer_cast<SimInstrument>(minstr);        
    }

    void SimExecutor::place_order(Order ord) {

        auto instrument = extract_instrument(ord);
        if (!instrument) {
            ord.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::not_tradable});
            return;
        }

        ActiveOrder aord{
            std::move(ord),std::move( instrument)
        };
        if (!validate_order(aord)) return;
        report(aord).on_order_accept(aord.ord, { generate_random_string()});        
        if (match_order(aord,true)) return;

        _active_orders.push_back(std::move(aord));

    }
    void SimExecutor::replace_order(Order ord, Order prev_order) {


        auto instrument = extract_instrument(ord);
        if (!instrument) {
            ord.update_order(OrderStatusUpdate{OrderStatus::rejected, OrderRejectionReason::not_tradable});
            return;
        }

        ActiveOrder aord{
            std::move(ord),std::move( instrument)
        };
        if (!validate_order(aord)) return;
        auto found = std::find_if(_active_orders.begin(), _active_orders.end(), [&](const ActiveOrder &a){
            return a.ord == prev_order;
        });
        if (found == _active_orders.end()) {
            report(aord).on_order_status(ord, {OrderStatus::rejected, OrderRejectionReason::order_not_found});
            return ;
        }
        if (!validate_order_replace(aord, *found)) return;
        report(aord).on_order_accept(aord.ord, {generate_random_string()});
        aord.filled = found->filled; //copy filled 
        report(aord).on_order_status(found->ord, {OrderStatus::replaced});        

        if (match_order(aord,true)) {
            _active_orders.erase(found);
        } else {
            *found = std::move(aord);
        }

    }
    void SimExecutor::cancel_order(Order ord) {
        auto found = std::find_if(_active_orders.begin(), _active_orders.end(), [&](const ActiveOrder &a){
            return a.ord == ord;
        });
        if (found == _active_orders.end()) return;  
        auto aord = std::move(*found);
        _active_orders.erase(found);
        report(aord).on_order_status(aord.ord, {OrderStatus::canceled});
    }

    bool SimExecutor::validate_order(ActiveOrder &order) {
        const Order &ord = order.ord;
        const OrderParametersGen<Decimal> &params = ord.get_parameters();

        if (params.quantity <= 0) {
            report(order).on_order_status(ord, { OrderStatus::rejected, OrderRejectionReason::invalid_params, "Invalid quantity" });
            return false;
        }

        if (params.type != OrderType::stop && params.type != OrderType::market && params.limit_price <= Decimal(0)) {
            report(order).on_order_status(ord, { OrderStatus::rejected,  OrderRejectionReason::invalid_params ,"Invalid or missing limit price"});
            return false;
        }

        if ((params.type == OrderType::stop || params.type == OrderType::stoplimit || params.type == OrderType::oco) && params.stop_price <= Decimal(0)) {
            report(order).on_order_status(ord, { OrderStatus::rejected,  OrderRejectionReason::invalid_params , "Invalid or missing stop price"});
            return false;
        }

        return true;
    }
    bool SimExecutor::validate_order_replace(ActiveOrder &order, const ActiveOrder &replacing_order) {
        const OrderParametersGen<Decimal> &params = order.ord.get_parameters();
        const OrderParametersGen<Decimal> &old_params = replacing_order.ord.get_parameters();
        if (params.side != old_params.side || params.type != old_params.type) {
            report(order).on_order_status(order.ord, {OrderStatus::rejected , OrderRejectionReason::invalid_replace});
            return false;
        }
        return true;
    }   

    
    bool SimExecutor::match_order(ActiveOrder &order, bool taker) {
        if (_last_quote.has_value()) {
            return match_order(order, _last_quote.value(), taker);
        } else {
            return false;
        }
    }
    bool SimExecutor::match_order(ActiveOrder &order, Quote &quote, bool taker) {
        auto &params  = order.ord.get_parameters();
        while (order.filled < params.quantity) {

            if (!_last_quote.has_value()) return false;            
            auto leave_quant = params.quantity - order.filled;
            auto &p = params.side == Side::sell?quote.bid:quote.ask;
            auto &s = params.side== Side::sell?quote.bid_size:quote.ask_size;
            Decimal dq = leave_quant - s;
            Decimal dp = params.limit_price - p;
            int sid = static_cast<int>(params.side);

            auto type = real_order_type(order);

            switch (type) {
                case OrderType::stop:
                case OrderType::stoplimit:
                case OrderType::oco:
                    dp = params.stop_price - p;
                    if (sgn(dp) * sid < 0) order.trig = true;  
                    else return false;
                    break;
                case OrderType::market:
                    if (dq > 0) {
                        create_fill(order, p, dq,quote.time,taker); 
                        s -= dq;
                    }
                    else create_fill(order, Decimal(p.to_double() + p.to_double() *_slippage*static_cast<double>(params.side)), leave_quant,quote.time,taker);
                    break;                

                case OrderType::limit_post_only:
                    if (taker && sgn(dp) * sid < 0) {
                        report(order).on_order_status(order.ord, {OrderStatus::rejected, OrderRejectionReason::post_only_taker});
                        return true;
                    }
                    [[fallthrough]];
                case OrderType::limit_ioc:
                case OrderType::limit:
                    if (dp == 0 && dq > 0) {
                        create_fill(order,p, dq, quote.time, taker);                            
                        break;
                    }
                    else if (sgn(dp) * sid < 0) {
                        create_fill(order, params.limit_price, leave_quant, quote.time, taker);
                        break;
                    }                        
                    if (params.type == OrderType::limit_ioc) {
                        report(order).on_order_status(order.ord, {OrderStatus::filled});
                        return true;
                    }
                    return false;                                                            
            }
        }

        report(order).on_order_status(order.ord, {OrderStatus::filled});
        return true;
    }

    OrderType SimExecutor::real_order_type(const ActiveOrder &order) {
        auto type = order.ord.get_parameters().type;
        if (order.trig) {
            switch (type) {
                case OrderType::oco: type = OrderType::market;break;
                case OrderType::stop: type = OrderType::market;break;
                case OrderType::stoplimit: type = OrderType::limit;break;
                default: break;                    
            };
        }
        return type;
    }

    bool SimExecutor::match_order(ActiveOrder &order, Trade &trade) {
        auto type = real_order_type(order);
        if (is_limit_order(type)) {
            auto &params = order.ord.get_parameters();
            if ((params.side == Side::buy && trade.price <= params.limit_price)
                || (params.side == Side::sell && trade.price >= params.limit_price))
            {                
                Decimal leave_quant = params.quantity - order.filled;
                create_fill(order, params.limit_price, std::max(leave_quant, trade.size), trade.time,false);
                bool filled = order.filled >= params.quantity;
                if (filled) report(order).on_order_status(order.ord, {OrderStatus::filled});
                return filled;
            }
        }
        if (is_stop_order(type)) {
            auto &params = order.ord.get_parameters();
            if ((params.side == Side::buy && trade.price >= params.stop_price)
                || (params.side == Side::sell && trade.price <= params.stop_price))
            {
                order.trig = true;
                return match_order(order, true);
            }
        }
        return false;

    }

    void SimExecutor::on_event(PSimInstrument instrument, Trade &trade){
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord){
            if (ord.instrument == instrument) {
                return match_order(ord, trade);
            } 
            return false;
        });
        _active_orders.erase(e, _active_orders.end());
    }

    void SimExecutor::on_event(PSimInstrument instrument, Quote &quote){
        Quote new_quote = quote;
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord){
            if (ord.instrument == instrument) {
                bool b =  match_order(ord, quote, false);
                if (b) return true;
                if (is_limit_order(real_order_type(ord) )) {
                    auto &p =ord.ord.get_parameters(); 
                    if (p.side == Side::sell) new_quote.ask = std::min(new_quote.ask, p.limit_price);
                    else if (p.side == Side::buy) new_quote.bid = std::max(new_quote.bid, p.limit_price);
                }
            } 
            return false;
        });
        _active_orders.erase(e, _active_orders.end());
        quote.bid = new_quote.bid;
        quote.ask = new_quote.ask;

    }


    void SimExecutor::create_fill(ActiveOrder &order, Decimal price, Decimal quantity, Timestamp tp, bool taker) {
        
        double volume = price.to_double() * quantity.to_double();
        double fees = (taker?_taker_fees:_maker_fees) * volume;
        Fill f{
            generate_random_string(),
            order.ord.get_name(),
            tp,
            order.ord.get_instrument()->get_info(),
            order.ord.get_parameters().side,
            order.ord.get_parameters().reason_override,
            quantity,
            price,
            fees,
            1.0
        };
        order.filled += quantity;
        report(order).on_order_fill(order.ord, f);
    }

bool SimExecutor::cancel_all(PTradableInstrument instrument) {
    auto iter = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](const ActiveOrder &x) {
        if (x.ord.get_instrument() == instrument)    {
            report(x).on_order_status(x.ord,{OrderStatus::canceled});
            return true;
        }
        return false;
    });
    if (iter != _active_orders.end()) {
        _active_orders.erase(iter, _active_orders.end());
        return true;
    }
    return false;
}



SimTradableInstrument &SimExecutor::report(const ActiveOrder &aord) {
    return *static_cast<SimTradableInstrument *>(aord.ord.get_instrument().get());
}
}




