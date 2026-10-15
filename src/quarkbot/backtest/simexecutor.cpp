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

    ///Has the market reached a stop/alert trigger price?
    /** Inclusive on both sides, same as the trade driven path: a buy triggers
     *  once the market is at or above the stop price, a sell at or below it.
     *  @param market_price ask for a buy, bid for a sell
     */
    static bool stop_reached(Decimal stop_price, Decimal market_price, Side side) {
        return sgn(stop_price - market_price) * static_cast<int>(side) <= 0;
    }

    ///Replace a missing quoted size with unlimited liquidity
    /** Some feeds carry no size information and report 0. That means "size
     *  unknown", not "nothing offered", so matching must not treat it as an
     *  empty book. Decimal::max() saturates on subtraction, so it never drains -
     *  which keeps s==0 unambiguously meaning "the quoted liquidity is used up".
     */
    static Quote with_normalized_sizes(Quote q) {
        if (q.bid_size <= 0) q.bid_size = Decimal::max();
        if (q.ask_size <= 0) q.ask_size = Decimal::max();
        return q;
    }

    ///Price a market order pays for the part that walks past the visible quote
    Decimal SimExecutor::slipped_price(Decimal price, Side side) const {
        return Decimal(price.to_double() + price.to_double() * _slippage * static_cast<double>(side));
    }

    PMarketInstrument SimExecutor::extract_instrument(const POrder &ord) {
        TradableInstrument instr( ord->get_instrument());
        MarketInstrument minstr = instr;
        return minstr.get_handle();        
    }

    void SimExecutor::place_new_order_internal(POrder ord) {


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

        if (admit_order(aord)) return;
        if (match_on_placement(aord)) return;
        if (finish_if_cannot_rest(aord)) return;

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

        if (admit_order(aord) || match_on_placement(aord) || finish_if_cannot_rest(aord)) {
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

    bool SimExecutor::finish_if_cannot_rest(ActiveOrder &order) {
        //ioc and fok never sit in the order book: whatever could not be filled
        //immediately is canceled. This also covers "no market data at all", where
        //matching had nothing to work with. Reported with a reason that maps to
        //canceled - the order did exactly what it was told, nothing failed.
        switch (order.time_in_force) {
            case TimeInForce::ioc:
            case TimeInForce::fok:
                set_order_status(order.ord, OrderRejectionReason::not_filled);
                return true;
            default:
                return false;
        }
    }

    bool SimExecutor::admit_order(ActiveOrder &order) {
        const auto &name = order.instrument->get_info().name;
        const auto &params = order.ord->get_parameters();
        if (is_auction_order(params.time_in_force)) {
            //an auction order only waits for its auction - never matched now
            auto rejection = _auctions.check_auction_order(name, params.time_in_force);
            if (rejection) {
                set_order_status(order.ord, *rejection);
                return true;
            }
            return false;
        }
        if (params.time_in_force == TimeInForce::day && _auctions.is_closed(name)) {
            set_order_status(order.ord, OrderRejectionReason::too_late);
            return true;
        }
        return false;
    }

    bool SimExecutor::match_on_placement(ActiveOrder &order) {
        if (is_auction_order(order.time_in_force)) return false;
        //in a call phase or after the close the order waits - an ioc/fok is
        //then canceled by finish_if_cannot_rest()
        if (!_auctions.can_match_continuous(order.instrument->get_info().name)) {
            order.held = true;
            return false;
        }
        return match_order(order, true);
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
        //match only against market data of the order's own instrument
        auto iter = _last_quote.find(order.instrument->get_info().name);
        if (iter == _last_quote.end()) return false;    //no market data yet
        return match_order(order, iter->second, taker);
    }

    

    bool SimExecutor::match_order(ActiveOrder &order, Quote quote, bool taker) {
        //auction orders and the phase of the day are handled before matching
        //(admit_order, can_match_continuous) - this only does the matching math
        auto &params  = order.ord->get_parameters();
        if (real_order_type(order) == OrderType::alert) {
            //an alert works like a stop that generates no fill - it just goes to
            //filled once triggered
            auto &p = params.side == Side::sell?quote.bid:quote.ask;
            if (!stop_reached(params.stop_price, p, params.side)) return false;
            set_order_status(order.ord, {OrderStatus::filled});
            return true;
        }
        while (order.calcs.filled < params.quantity) {

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
                    //not triggered yet - the stop fires once the quote reaches
                    //the stop price, then the order continues as market/limit
                    if (!stop_reached(params.stop_price, p, params.side)) return false;
                    order.trig = true;
                    continue;

                case OrderType::market:
                    if (dq > 0) {
                        //book shows less than we need: take all of the visible
                        //liquidity at the touch...
                        if (s > 0) {
                            create_fill(order, p, s, quote.time, taker);
                            s = 0;
                        }
                        //...and walk the rest deeper into the book, which the
                        //quote cannot describe - that part pays slippage
                        Decimal rest = params.quantity - order.calcs.filled;
                        if (rest > 0) {
                            create_fill(order, slipped_price(p, params.side), rest, quote.time, taker);
                        }
                    } else {
                        create_fill(order, slipped_price(p, params.side), leave_quant, quote.time, taker);
                        s -= leave_quant;   //consume the liquidity we just took
                    }
                    break;

                case OrderType::limit_post_only:
                    //only a price that is strictly more aggressive than the touch
                    //takes liquidity. Sitting exactly on the touch is fine and
                    //must not be refused.
                    if (taker && sgn(dp) * sid > 0) {
                        set_order_status(order.ord,  OrderRejectionReason::post_only_taker);
                        return true;
                    }
                    [[fallthrough]];
                case OrderType::limit:
                    //dp == 0 sits exactly on the touch, sgn(dp)*sid > 0 crosses it.
                    //(when dp == 0 the limit price and the touch are the same value)
                    if (sgn(dp) * sid >= 0 && s > 0) {
                        //fill-or-kill must not fill partially - without the whole
                        //quantity available at the touch it is killed instead
                        if (params.time_in_force == TimeInForce::fok && s < leave_quant) {
                            return false;
                        }
                        //only the quoted L1 size can trade in this event - the
                        //next book level is unknown, and assumed to be far
                        //enough away that it is not reached here. The rest of
                        //the order stays in the book as a partial fill.
                        Decimal fill_quant = std::min(leave_quant, s);
                        //post_only is refused outright above when it would really
                        //take liquidity, so any fill it still gets is a maker fill
                        //by definition. It must never be charged the taker fee -
                        //guaranteeing the maker side is the whole point of the flag.
                        bool as_taker = taker && type != OrderType::limit_post_only;
                        //An order that crosses the moment it is placed takes the
                        //touch, so it gets the market price - which is better
                        //than its own limit. A resting order that the market
                        //later crosses is the maker, so its own limit price is
                        //the trade price. That is exactly what taker tells us.
                        Decimal fill_price = as_taker?p:params.limit_price;
                        create_fill(order, fill_price, fill_quant, quote.time, as_taker);
                        s -= fill_quant;
                        break;
                    }
                    //nothing (more) can trade here. Whether the remainder rests
                    //in the book or is canceled is decided by the time in force,
                    //see finish_if_cannot_rest() at the placement site.
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
                //a held order is a taker of this print (post-only never is - it
                //must stay on the maker side); a resting order is the maker and
                //trades at its own limit
                bool as_taker = order.held && type != OrderType::limit_post_only;
                Decimal price = as_taker ? trade.price : params.limit_price;
                create_fill(order, price, std::min(leave_quant, trade.size), trade.time, as_taker);
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

    void SimExecutor::on_event(PMarketInstrument instrument, Trade &trade){
        seed_random(trade.time);
        const auto &name = instrument->get_info().name;
        auto act = _auctions.on_trade(name, trade.qualifier, trade.time);
        if (act.uncross) {
            uncross(instrument, to_auction_type(trade.qualifier), trade.price, trade.size, trade.time);
        }
        apply_auction_actions(instrument, act, trade.time);
        //an uncross is the auction itself, it is not matched once more as a trade
        if (act.uncross || !_auctions.can_match_continuous(name)) return;
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord){
            if (ord.instrument == instrument && !is_auction_order(ord.time_in_force)) {
                bool done = match_order(ord, trade);
                //the first continuous evaluation is over, filled or not
                ord.held = false;
                return done;
            }
            return false;
        });
        _active_orders.erase(e, _active_orders.end());
    }

    void SimExecutor::on_event(PMarketInstrument instrument, Quote &quote){
        seed_random(quote.time);
        //new_quote starts as the raw feed data and collects the limit prices of
        //resting orders; book is what matching works on, so the liquidity our
        //own orders consume never leaks into the published market data.
        //match_order takes the quote by value, so every order sees the same
        //untouched book - see its declaration for why.
        Quote new_quote = quote;
        Quote book = with_normalized_sizes(quote);
        const auto &name = instrument->get_info().name;
        _last_quote[name] = book;
        //the quote is remembered always, but only matched in continuous trading
        bool can_match = _auctions.can_match_continuous(name);
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord){
            //auction orders are not in the continuous book
            if (ord.instrument == instrument && !is_auction_order(ord.time_in_force)) {
                if (can_match) {
                    //a held order takes liquidity (post-only stays on the maker side)
                    bool taker = ord.held && real_order_type(ord) != OrderType::limit_post_only;
                    bool done = match_order(ord, book, taker);
                    ord.held = false;
                    if (done) return true;
                }
                //limits are reflected into the published quote only in continuous
                //trading - otherwise a limit beyond the other side would publish
                //a crossed quote (and a crossed mid price to the account)
                if (can_match && is_limit_order(real_order_type(ord) )) {
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

    void SimExecutor::on_event(PMarketInstrument instrument, Auction &auction_data) {
        seed_random(auction_data.time);
        //indicative data only drives the phase - it never fills anything
        auto act = _auctions.on_indicative(instrument->get_info().name, auction_data.auction_type);
        apply_auction_actions(instrument, act, auction_data.time);
    }

    void SimExecutor::apply_auction_actions(const PMarketInstrument &instrument, const AuctionPhaseTracker::Actions &act, Timestamp tp) {
        if (act.finish) finish_auction(instrument, *act.finish);
        if (act.close_day) close_day(instrument);
        if (act.start_close_timeout) expire_auction(instrument, tp);
    }

    void SimExecutor::uncross(const PMarketInstrument &instrument, AuctionType type, Decimal price, Decimal size, Timestamp tp) {
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord) {
            if (ord.instrument != instrument) return false;
            const auto &params = ord.ord->get_parameters();
            //an auction order takes part in its own auction only
            if (is_auction_order(params.time_in_force)
                    && !is_valid_order_for_auction_type(params.time_in_force, type)) {
                return false;
            }
            auto otype = real_order_type(ord);
            if (otype == OrderType::stop || otype == OrderType::alert) {
                //the auction price triggers stops, but a triggered stop joins
                //the next phase, not this uncross - same as on a real venue
                if (!stop_reached(params.stop_price, price, params.side)) return false;
                if (otype == OrderType::alert) {
                    set_order_status(ord.ord, {OrderStatus::filled});
                    return true;
                }
                ord.trig = true;
                return false;
            }
            Decimal leave_quant = params.quantity - ord.calcs.filled;
            Decimal fill_quant;
            if (otype == OrderType::market) {
                //the simulator is not a market participant - the printed
                //volume does not limit a market order
                fill_quant = leave_quant;
            } else if (is_limit_order(otype) && otype != OrderType::limit_post_only) {
                //post-only never takes part - it rests untouched into the
                //continuous phase, keeping the maker guarantee
                bool crosses = (params.side == Side::buy && params.limit_price >= price)
                            || (params.side == Side::sell && params.limit_price <= price);
                if (!crosses) return false;
                //every participant gets the single auction price; the print
                //size roughly models a partial execution
                fill_quant = std::min(leave_quant, size);
            } else {
                return false;
            }
            if (fill_quant > 0) create_fill(ord, price, fill_quant, tp, true);
            if (ord.calcs.filled >= params.quantity) {
                set_order_status(ord.ord, {OrderStatus::filled});
                return true;
            }
            //the remainder is a leftover of the auction - see ActiveOrder::held
            if (fill_quant > 0) ord.held = true;
            return false;
        });
        _active_orders.erase(e, _active_orders.end());
    }

    void SimExecutor::finish_auction(const PMarketInstrument &instrument, AuctionType type) {
        auto e = std::remove_if(_active_orders.begin(), _active_orders.end(), [&](ActiveOrder &ord) {
            if (ord.instrument != instrument) return false;
            auto tif = ord.ord->get_parameters().time_in_force;
            if (!is_auction_order(tif) || !is_valid_order_for_auction_type(tif, type)) return false;
            //the auction is over, so an unfilled remainder can no longer trade -
            //that is expired, not filled
            set_order_status(ord.ord, OrderRejectionReason::expired);
            return true;
        });
        _active_orders.erase(e, _active_orders.end());
    }

    void SimExecutor::close_day(PMarketInstrument instrument) {
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
        //fees must respect the contract geometry (multiplier, tick scale) and,
        //for inverse contracts, the fact that the two currencies differ:
        //  - fees        is denominated in the quote currency
        //  - fees_native is what the exchange actually charges, which for an
        //                inverse contract is the pnl (settlement) currency
        //For a plain linear contract both turnovers are equal, so both fees are.
        Decimal fee_rate = taker?info.fee_rate_taker:info.fee_rate_maker;
        Decimal turnover_pnl = info.calc_turnover_pnl_currency(price, quantity);
        Decimal fees = fee_rate * info.calc_turnover_quote_currency(price, quantity);
        Decimal fees_native = fee_rate * turnover_pnl;
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
        order.calcs.turnover += turnover_pnl;
        order.calcs.fees_native += fees_native;
        auto &simt = *static_cast<SimTradableInstrument *>(order.ord->get_instrument().get());
        if (_report_sink) {
            _report_sink(order.ord, f);
            _report_sink(order.ord, order.calcs);
        }
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

StrategyFragment SimExecutor::place_new_order(POrder ord) {
    if (co_await _timer.sleep_for(latency)){
        place_new_order_internal(std::move(ord));
    } else {
        auto &simt = *static_cast<SimTradableInstrument *>(ord->get_instrument().get());    
        simt.on_order_update(ord, OrderRejectionReason::adapter_stopped);
    }
}
StrategyFragment SimExecutor::replace_order(POrder ord, POrder prev_order) {
    if (co_await _timer.sleep_for(latency)){
        place_order_internal(std::move(ord), std::move(prev_order));
    } else {
        auto &simt = *static_cast<SimTradableInstrument *>(ord->get_instrument().get());    
        simt.on_order_update(ord, OrderRejectionReason::adapter_stopped);
    }
    
}
StrategyFragment SimExecutor::cancel_order(POrder ord) {
    return cancel_order(ord.get());
}
StrategyFragment SimExecutor::cancel_order(IOrder *ord) {
    co_await _timer.sleep_for(latency); //if timer is cancelled, perform immediately cancel (backtest is finished)
    cancel_order_internal(ord);    
}

void SimExecutor::stop_on(std::stop_token tkn) {
    _timer.stop_on(tkn);
}

void SimExecutor::stop_latency_queue() {
    _timer.cancel();
    ExecutionWorker::current().quiesce();    

}

StrategyFragment SimExecutor::expire_auction(PMarketInstrument instrument, std::chrono::system_clock::time_point tp) {
    if (co_await _timer.sleep_until(tp+std::chrono::minutes(30))) {
        auto act = _auctions.on_close_timeout(instrument->get_info().name);
        apply_auction_actions(instrument, act, tp);
    }
}

void SimExecutor::seed_random(std::chrono::system_clock::time_point tp) {
    //seed generator with timestamp to make deterministict ID's
    _rnd_gen.seed(static_cast<std::default_random_engine::result_type>(tp.time_since_epoch().count()));
}
}




