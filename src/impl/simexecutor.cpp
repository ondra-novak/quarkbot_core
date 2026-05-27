#include "simexecutor.hpp"
#include "ifc/order_defs.hpp"
#include "impl/simexchange.hpp"
#include "siminstrument.hpp"    
#include "ifc/defs.hpp"
#include "ifc/order.hpp"
#include "ifc/types.hpp"
#include "utils/decimal.hpp"
#include "utils/random_string.hpp"
#include "simtradableinstrument.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
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
            set_order_status(ord,  OrderRejectionReason::not_tradable);
            return;
        }

        Decimal filled = ord.get_filled();
        ActiveOrder aord{
            std::move(ord),std::move( instrument), filled
        };
        if (!validate_order(aord)) return;

        accept_order(aord.ord);

        if (match_order(aord,true)) return;

        _active_orders.push_back(std::move(aord));

    }
    void SimExecutor::replace_order(Order ord, Order prev_order) {


        auto instrument = extract_instrument(ord);
        if (!instrument) {
            set_order_status(ord,  OrderRejectionReason::not_tradable);
            return;
        }

        ActiveOrder aord{
            std::move(ord),std::move( instrument)
        };
        if (!validate_order(aord)) return;

        accept_order(aord.ord);

        auto found = std::find_if(_active_orders.begin(), _active_orders.end(), [&](const ActiveOrder &a){
            return a.ord == prev_order;
        });

        if (found == _active_orders.end()) {
            set_order_status(aord.ord,  OrderRejectionReason::order_not_found);
            return ;
        }
        if (!validate_order_replace(aord, *found)) return;
        aord.filled = found->filled; //copy filled 

        set_order_status(found->ord, {OrderStatus::replaced});        

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
        set_order_status(aord.ord, {OrderStatus::canceled});
    }

    bool SimExecutor::validate_order(ActiveOrder &order) {
        const Order &ord = order.ord;
        const OrderParametersGen<Decimal> &params = ord.get_parameters();

        if (params.quantity <= 0 && params.type != OrderType::alert) {
            set_order_status(ord, Order::RejectionWithText{ OrderRejectionReason::invalid_params, "Invalid quantity" });
            return false;
        }
        if (is_stop_order(params.type) && params.stop_price <= Decimal(0)) {
            set_order_status(ord, Order::RejectionWithText{  OrderRejectionReason::invalid_params ,"Invalid or missing stop price"});
            return false;
        }
        if (is_limit_order(params.type) && params.limit_price <= Decimal(0)) {
            set_order_status(ord, Order::RejectionWithText{ OrderRejectionReason::invalid_params , "Invalid or missing limit price"});
            return false;
        }
        if (params.time_in_force != TimeInForce::gtc && params.time_in_force != TimeInForce::ioc) {
            set_order_status(ord, Order::RejectionWithText{ OrderRejectionReason::invalid_params, "Unsupported time in force"});
            return false;
        }

        return true;
    }
    bool SimExecutor::validate_order_replace(ActiveOrder &order, const ActiveOrder &replacing_order) {
        const OrderParametersGen<Decimal> &params = order.ord.get_parameters();
        const OrderParametersGen<Decimal> &old_params = replacing_order.ord.get_parameters();
        if (params.side != old_params.side || params.type != old_params.type) {
            set_order_status(order.ord, OrderRejectionReason::invalid_replace);
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
        {
            auto &p = params.side == Side::sell?quote.bid:quote.ask;
            int sid = static_cast<int>(params.side);
            if (real_order_type(order) == OrderType::alert) {
                Decimal dp = params.stop_price - p;
                if (sgn(dp) * sid < 0) {
                    set_order_status(order.ord, {OrderStatus::filled});
                    return true;
                }
                return false;
            }
        }
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
                    if (taker && sgn(dp) * sid > 0) {
                        set_order_status(order.ord,  OrderRejectionReason::post_only_taker);
                        return true;
                    }
                    [[fallthrough]];
                case OrderType::limit:
                    if (dp == 0) {
                        create_fill(order, p, leave_quant, quote.time, taker);
                        break;
                    }
                    else if (sgn(dp) * sid > 0) {
                        create_fill(order, params.limit_price, leave_quant, quote.time, taker);
                        break;
                    }
                    if (params.time_in_force == TimeInForce::ioc) {
                        set_order_status(order.ord, {OrderStatus::filled});
                        return true;
                    }
                    return false;
                case OrderType::alert:
                    break; // handled above, unreachable
            }
        }

        set_order_status(order.ord, {OrderStatus::filled});
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
                create_fill(order, params.limit_price, std::min(leave_quant, trade.size), trade.time,false);
                bool filled = order.filled >= params.quantity;
                if (filled) set_order_status(order.ord, {OrderStatus::filled});
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
        _last_quote = quote;
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
            {static_cast<std::uint64_t>(tp.time_since_epoch().count()),_random_key++},
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
        auto &simt = *static_cast<SimTradableInstrument *>(order.ord.get_instrument().get());
        simt.on_order_update(order.ord, f);
    }

bool SimExecutor::cancel_all(PTradableInstrument instrument) {
    auto iter = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](const ActiveOrder &x) {
        if (x.ord.get_instrument() == instrument)    {
            set_order_status(x.ord,{OrderStatus::canceled});
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

void SimExecutor::set_order_status(const Order &ord, Order::Update &&st) {
    auto &simt = *static_cast<SimTradableInstrument *>(ord.get_instrument().get());    
    simt.on_order_update(ord, std::move(st));
}

void  SimExecutor::accept_order(const Order &ord) {
    auto &simt = *static_cast<SimTradableInstrument *>(ord.get_instrument().get());
    std::string id = generate_random_string();
    std::hash<std::string> hasher;
    RecordKey rk({
        static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
        hasher(id)
    });
    simt.on_order_update(ord, Order::OpenStatus{id, rk});
}

}




