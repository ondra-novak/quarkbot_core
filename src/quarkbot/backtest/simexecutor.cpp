#include "simexecutor.hpp"
#include "../common/orderdata.hpp"
#include <quarkbot/execution_worker.hpp>
#include <quarkbot/order_defs.hpp>
#include <quarkbot/stream/auction.hpp>
#include <quarkbot/defs.hpp>
#include <quarkbot/order.hpp>
#include <quarkbot/types.hpp>
#include <quarkbot/tradable_instrument.hpp>
#include <quarkbot/decimal.hpp>
#include <quarkbot/utils/random_string.hpp>
#include "../common/orderdata.hpp"
#include "quarkbot/market_instrument.hpp"
#include "simexchange.hpp"
#include "siminstrument.hpp"    
#include "simtradableinstrument.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>

namespace quarkbot {

    SimExecutor::PSimInstrument SimExecutor::extract_instrument(const POrder &ord) {
        TradableInstrument instr( ord->get_instrument());
        MarketInstrument minstr = instr;
        return std::dynamic_pointer_cast<SimInstrument>(minstr.get_handle());        
    }

    void SimExecutor::place_order_internal(POrder ord) {


        auto instrument = extract_instrument(ord);
        if (!instrument) {
            set_order_status(ord,  OrderRejectionReason::not_tradable);
            return;
        }

        POrderData ordd = std::dynamic_pointer_cast<OrderInternalData>(ord);
        if (!ordd) {
            set_order_status(ord,  OrderRejectionReason::unsupported);
            return ;
        }

        const auto &nxrep = ordd->get_fill_stats();
        auto tif = ord->get_parameters().time_in_force; 
        ActiveOrder aord{
            std::move(ord),std::move( instrument), tif, nxrep    };
        if (!validate_order(aord)) return;

        accept_order(aord.ord);

        if (match_order(aord,true)) return;

        _active_orders.push_back(std::move(aord));

    }
    void SimExecutor::place_order_internal(POrder ord, POrder prev_order) {


        auto instrument = extract_instrument(ord);
        if (!instrument) {
            set_order_status(ord,  OrderRejectionReason::not_tradable);
            return;
        }

        auto tif = ord->get_parameters().time_in_force;
        ActiveOrder aord{
            std::move(ord),std::move( instrument),tif,{}
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
        

        set_order_status(found->ord, {OrderStatus::replaced});        

        if (match_order(aord,true)) {
            _active_orders.erase(found);
        } else {
            *found = std::move(aord);
        }

    }
    void SimExecutor::cancel_order_internal(IOrder *ord) {
        auto found = std::find_if(_active_orders.begin(), _active_orders.end(), [&](const ActiveOrder &a){
            return a.ord.get() == ord;
        });
        if (found == _active_orders.end()) return;  
        auto aord = std::move(*found);
        _active_orders.erase(found);
        set_order_status(aord.ord, {OrderStatus::canceled});
    }

    bool SimExecutor::validate_order(ActiveOrder &order) {
        const POrder &ord = order.ord;
        const OrderParametersGen<Decimal> &params = ord->get_parameters();

        if (params.quantity <= 0 && params.type != OrderType::alert) {
            set_order_status(ord, OrderRejectionWithText{ OrderRejectionReason::invalid_params, "Invalid quantity" });
            return false;
        }
        if (is_stop_order(params.type) && params.stop_price <= Decimal(0)) {
            set_order_status(ord, OrderRejectionWithText{  OrderRejectionReason::invalid_params ,"Invalid or missing stop price"});
            return false;
        }
        if (is_limit_order(params.type) && params.limit_price <= Decimal(0)) {
            set_order_status(ord, OrderRejectionWithText{ OrderRejectionReason::invalid_params , "Invalid or missing limit price"});
            return false;
        }
        if (is_auction_order(params.time_in_force) && !is_valid_auction_order(params.type))  {
            set_order_status(ord, OrderRejectionWithText{ OrderRejectionReason::invalid_params, "Invalid time-in-force for order type"});
            return false;
        }

        return true;
    }
    bool SimExecutor::validate_order_replace(ActiveOrder &order, const ActiveOrder &replacing_order) {
        const OrderParametersGen<Decimal> &params = order.ord->get_parameters();
        const OrderParametersGen<Decimal> &old_params = replacing_order.ord->get_parameters();
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
        auto &params  = order.ord->get_parameters();
        //attempt to find auction information about this instrument
        const auto &info  = order.ord->get_instrument()->get_instrument()->get_info();        
        auto auction_state_iter = _auction_state.find(info.name);        

        //if order is auction order
        if (is_auction_order(params.time_in_force)) {            
            //no auction information, nor open auction is not yet started
            if (auction_state_iter == _auction_state.end() || !auction_state_iter->second.at_open_started) {
                //reject
                set_order_status(order.ord, OrderRejectionReason::not_tradable);
                return true;
            }
            //close auction already finished (day closed)
            if (auction_state_iter->second.at_close_finished) {
                //reject
                set_order_status(order.ord,OrderRejectionReason::too_late);
                return true;
            }            
            const auto &params = order.ord->get_parameters();
            //order is ato but open auction finished
            if (params.time_in_force == TimeInForce::ato && auction_state_iter->second.at_open_finished) {
                //reject
                set_order_status(order.ord,OrderRejectionReason::too_late);
                return true;
            }
            //keep order
            return false;
        }
        if (params.time_in_force == TimeInForce::day  && auction_state_iter != _auction_state.end() 
                    && auction_state_iter->second.at_close_finished) {
            set_order_status(order.ord,OrderRejectionReason::too_late);
            return true;
        }        
        if (real_order_type(order) == OrderType::alert) {        
            auto &p = params.side == Side::sell?quote.bid:quote.ask;
            int sid = static_cast<int>(params.side);
            Decimal dp = params.stop_price - p;
            if (sgn(dp) * sid < 0) {
                set_order_status(order.ord, {OrderStatus::filled});
                return true;
            }
            return false;
        }
        while (order.calcs.filled < params.quantity) {

            if (!_last_quote.has_value()) return false;
            auto leave_quant = params.quantity - order.calcs.filled;
            auto &p = params.side == Side::sell?quote.bid:quote.ask;
            auto &s = params.side== Side::sell?quote.bid_size:quote.ask_size;
            Decimal dq = leave_quant - s;
            Decimal dp = params.limit_price - p;
            int sid = static_cast<int>(params.side);

            auto type = real_order_type(order);

            switch (type) {
                case OrderType::stop:
                case OrderType::stoplimit:
/*                case OrderType::oco:
                    dp = params.stop_price - p;
                    if (sgn(dp) * sid < 0) order.trig = true;
                    else return false;
                    break;*/
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
        auto type = order.ord->get_parameters().type;
        if (order.trig) {
            switch (type) {
//                case OrderType::oco: type = OrderType::market;break;
                case OrderType::stop: type = OrderType::market;break;
                case OrderType::stoplimit: type = OrderType::limit;break;
                default: break;                    
            };
        } else {
            if (type == OrderType::stoplimit) return OrderType::stop;
        }
        return type;
    }

    bool SimExecutor::match_order(ActiveOrder &order, Trade &trade) {
        auto type = real_order_type(order);
        if (is_limit_order(type)) {
            auto &params = order.ord->get_parameters();
            if ((params.side == Side::buy && trade.price <= params.limit_price)
                || (params.side == Side::sell && trade.price >= params.limit_price))
            {                
                Decimal leave_quant = params.quantity - order.calcs.filled;
                create_fill(order, params.limit_price, std::min(leave_quant, trade.size), trade.time,false);
                bool filled = order.calcs.filled >= params.quantity;
                if (filled) set_order_status(order.ord, {OrderStatus::filled});
                return filled;
            }
        }
        if (is_stop_order(type)) {
            auto &params = order.ord->get_parameters();
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
        seed_random(trade.time);
        const auto &info = instrument->get_info();
        auto auction_iter = _auction_state.find(info.name);
        //first continuous-phase trade signals end of open auction (if one was started)
        if (auction_iter != _auction_state.end() && !auction_iter->second.at_open_finished) {
            auction_iter->second.at_open_finished = true;
            auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord) {
                if (ord.instrument == instrument) {
                    const auto &params = ord.ord->get_parameters();
                    if (params.time_in_force == TimeInForce::ato) {
                        set_order_status(ord.ord, OrderRejectionReason::expired);
                        return true;
                    }
                }
                return false;
            });
            _active_orders.erase(e, _active_orders.end());
        }
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord){
            if (ord.instrument == instrument) {
                return match_order(ord, trade);
            } 
            return false;
        });
        _active_orders.erase(e, _active_orders.end());
    }

    void SimExecutor::on_event(PSimInstrument instrument, Quote &quote){
        seed_random(quote.time);
        _last_quote = quote;
        Quote new_quote = quote;
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord){
            if (ord.instrument == instrument) {
                bool b =  match_order(ord, quote, false);
                if (b) return true;
                if (is_limit_order(real_order_type(ord) )) {
                    auto &p =ord.ord->get_parameters(); 
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

    void SimExecutor::on_event(PSimInstrument instrument, Auction &auction_data) {
        seed_random(auction_data.time);

        bool is_open_auction = auction_data.auction_type == AuctionType::opening;
        bool is_close_auction = auction_data.auction_type == AuctionType::closing;

        auto &st = _auction_state[instrument->get_info().name];        
        if (is_open_auction) {
            st.at_open_started = true;
            st.at_open_finished = false;
            st.at_close_started = false;
            st.at_close_finished = false;
        } else if (is_close_auction) {
            if (!st.at_close_started) {
                st.at_close_started = true;
                expire_auction(instrument, auction_data.time);
            }
            st.at_close_finished = false;
        }

        if (auction_data.final) {
            //auction is done
            auto iter = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord) {
                if (ord.instrument == instrument) {
                    const auto &params = ord.ord->get_parameters();
                    if (is_valid_order_for_auction_type(params.time_in_force, auction_data.auction_type)) {

                        if (params.type == OrderType::market
                            //or type is limit
                            || (params.type == OrderType::limit && 
                                //and depend on side, check price
                                ((params.side == Side::buy && params.limit_price >= auction_data.price)
                                || (params.side == Side::sell && params.limit_price <= auction_data.price))
                            )    
                        ) {
                                create_fill(ord, auction_data.price, std::min(auction_data.quantity, params.quantity), auction_data.time, true);
                                //send status
                                set_order_status(ord.ord, OrderStatus::filled);
                                //remove order
                                return true;
                        } else {
                            //order did not matched - send expired
                            set_order_status(ord.ord, OrderRejectionReason::expired);
                            //remove order
                            return true;

                        }
                    }
                }
                //don't remove any other order (yet)
                return false;
            });
            _active_orders.erase(iter, _active_orders.end());
            if (is_open_auction) st.at_open_finished = true;
            if (is_close_auction) {
                st.at_close_finished = true;
                close_day(instrument);
            }
        }
    }

    void SimExecutor::close_day(PSimInstrument instrument) {
        auto iter = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord){
            if (ord.instrument == instrument) {
                const auto &params = ord.ord->get_parameters();
                if (params.time_in_force != TimeInForce::gtc) {
                    set_order_status(ord.ord, OrderRejectionReason::expired);
                    return true;
                }
            }
            return false;
        });
         _active_orders.erase(iter, _active_orders.end());
    }

    void SimExecutor::create_fill(ActiveOrder &order, Decimal price, Decimal quantity, Timestamp tp, bool taker) {
        
        const auto &info = order.instrument->get_info();
        Decimal volume = price * quantity;
        Decimal fees = (taker?info.fee_rate_taker:info.fee_rate_maker) * volume;
        const auto &params = order.ord->get_parameters();
        Fill f{
            {static_cast<std::uint64_t>(tp.time_since_epoch().count()),_random_key++},
            generate_random_string(_rnd_gen),
            params.label,
            tp,
            info,
            params.side,
            params.reason_override,
            quantity,
            price
        };
        order.calcs.fees += fees;
        order.calcs.filled += quantity;
        order.calcs.turnover += info.calc_turnover_pnl_currency(price, quantity);
        order.calcs.fees_native += fees;
        auto &simt = *static_cast<SimTradableInstrument *>(order.ord->get_instrument().get());
        if (_report_sink) _report_sink(order.ord, f);
        simt.on_order_update(order.ord, order.calcs);
        simt.on_order_update(order.ord, f);
    }

bool SimExecutor::cancel_all(PTradableInstrument instrument) {
    bool r = false;
    for (auto &x: _active_orders) {
        if (x.ord->get_instrument() == instrument) {
            cancel_order(x.ord);
            r = true;
        }        
    }
    return r;
}

void SimExecutor::set_order_status(const POrder &ord, OrderInternalData::Update &&st) {
    auto &simt = *static_cast<SimTradableInstrument *>(ord->get_instrument().get());    
    if (_report_sink) _report_sink(ord, st);
    simt.on_order_update(ord, std::move(st));
}

void  SimExecutor::accept_order(const POrder &ord) {
    std::string id = generate_random_string(_rnd_gen);
    std::hash<std::string> hasher;
    RecordKey rk({
        static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
        hasher(id)
    });
    set_order_status(ord, OrderOpenStatus{id, rk});
}

StrategyFragment SimExecutor::place_order(POrder ord) {
    if (co_await _timer.sleep_for(latency)){
        place_order_internal(std::move(ord));
    }
}
StrategyFragment SimExecutor::replace_order(POrder ord, POrder prev_order) {
    if (co_await _timer.sleep_for(latency)){
        place_order_internal(std::move(ord), std::move(prev_order));
    }
    
}
StrategyFragment SimExecutor::cancel_order(POrder ord) {
    return cancel_order(ord.get());
}
StrategyFragment SimExecutor::cancel_order(IOrder *ord) {
    if (co_await _timer.sleep_for(latency)){
        cancel_order_internal(ord);
    }
}

void SimExecutor::stop_latency_queue() {
    _timer.cancel();
    ExecutionWorker::current().quiesce();    

}

StrategyFragment SimExecutor::expire_auction(PSimInstrument instrument, std::chrono::system_clock::time_point tp) {
    if (co_await _timer.sleep_until(tp+std::chrono::minutes(30))) {
        auto &st = _auction_state[instrument->get_info().name];
        if (!st.at_close_finished) {
            st.at_close_finished = true;
            close_day(instrument);
        }
    }
}

SimExecutor::~SimExecutor() {
    stop_latency_queue();
}

void SimExecutor::seed_random(std::chrono::system_clock::time_point tp) {
    //seed generator with timestamp to make deterministict ID's
    _rnd_gen.seed(static_cast<std::default_random_engine::result_type>(tp.time_since_epoch().count()));
}
}




