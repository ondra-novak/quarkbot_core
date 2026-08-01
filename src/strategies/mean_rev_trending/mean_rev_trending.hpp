#pragma once

#include "quarkbot/abstract/ieventstream.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/serie_persistent.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/timer.hpp"
#include "quarkbot/timestamp.hpp"
#include "quarkbot/tradable_instrument.hpp"
#include "quarkbot/ta/bollinger.hpp"
#include "quarkbot/ta/difference.hpp"
#include "quarkbot/ta/ema.hpp"
#include "quarkbot/types.hpp"
#include <chrono>

namespace {
    using namespace quarkbot;

using BBEma = ta::BollingerBandsGen<ta::EMA<PersistentSerie<Decimal> > > ;
using Ema = ta::EMA<PersistentSerie<Decimal> >;
using Diff = ta::Difference<PersistentSerie<Decimal> >;

class MeanRevTrendingStrategy {
    StrategyContext context;
    Timer timer = {};

public:
    MeanRevTrendingStrategy(StrategyContext &&context):context(std::move(context)) {}


    StrategyFragment main() {

        for (auto &instrument: context.instruments) {
            context.run(run_instrument(std::move(instrument), context.config/instrument.get_info().name));
        }
        co_return;
    }

    struct ParsedConfig {
        std::size_t trend_detection_min;
        std::size_t base_interval_min;        
        Decimal bbema_multiplier;
        Decimal reversal_multiplier;
        Decimal max_loss;
    };


    struct StrategyState {
        Decimal position = {};
        Decimal prev_price = {};
        Decimal total_loss = {};
        Decimal calc_loss = {};
    };

    struct State {
        ParsedConfig config;
        BBEma price_bb;        
        Ema trend_ema;               
        Ema trend_ema2;
        EventStream<Quote> quote_stream;
        TradableInstrument instrument;
        ContractInfo contract_type;

        StrategyState sstate = {};


        Decimal upper_price = {};
        Decimal lower_price = {};
        Order upper = {};
        Order lower = {};
        int cur_line = 0;
    };

    using PState = std::shared_ptr<State>;

    Order calculate_order(PState state, Order replaced_order, Decimal price, Side side, bool reverse) {
        auto [total, calc] = calculate_loss(state, price);
        Decimal new_pos = calc * state->config.reversal_multiplier / price;
        
        if (reverse) {
            //TODO: add loss on reverse
            Decimal new_pos_dir = static_cast<int>(side) * new_pos;
            Decimal diff = new_pos_dir - state->sstate.position ;
            return state->instrument.place_order({
                "reverse",
                side,
                OrderType::market,
                abs(diff),                
            });
        } else {
            Decimal new_pos_dir = sgn(state->sstate.position) * new_pos;
            Decimal diff = new_pos_dir - state->sstate.position ;
            return state->instrument.place_order({
                "dca",
                side,
                OrderType::limit,
                abs(diff),                
                price
            },replaced_order);
        }


    }

    std::pair<Decimal, Decimal> calculate_loss(PState state, Decimal cur_price) {
        Decimal pnl = state->contract_type.calc_pnl(state->sstate.prev_price, cur_price, state->sstate.position);
        Decimal new_loss = state->sstate.total_loss - pnl;
        Decimal new_calc_loss = pnl > 0?state->sstate.calc_loss - pnl:state->sstate.calc_loss - 2*pnl;
        Decimal total_loss = std::max(Decimal{}, new_loss);
        Decimal calc_loss = std::min(state->sstate.total_loss,std::max(Decimal{}, new_calc_loss));
        return {total_loss, calc_loss};
    }

    void process_fill(PState state, const Fill &f) {        
        auto x = calculate_loss(state, f.price);
        state->sstate.total_loss = x.first;
        state->sstate.calc_loss = x.second;
        state->sstate.position += f.quantity * static_cast<int>(f.side);

    }

   
    StrategyFragment monitor_order(PState state, Order order) {
        OrderReport rpt;
        while (true) {
            while (co_await order.receive(rpt)) {
                for (auto &f: rpt.fills) {
                    process_fill(state, f);
                }
            }
            if (order == state->lower) {
                state->lower = order =  calculate_order(state, {}, state->lower_price, Side::buy, false);                 
            } else if (order == state->upper) {
                state->lower = order =  calculate_order(state, {}, state->upper_price, Side::sell, false); 
            } else {
                break;
            }
        }
    }


    StrategyFragment tick_loop(PState state) {
        std::size_t base_interval = state->config.base_interval_min;
        Decimal bb_multipler = state->config.bbema_multiplier;
        Timestamp next_tp;                
        Quote qt;
        do {
            next_tp = interval_upper_bound(timer.now(), std::chrono::minutes(base_interval));
            state->quote_stream.current(qt); //read quotes
            if (!qt.both_sides()) continue;
            
            auto bbval = state->price_bb.update(qt.mid());
            state->upper_price = bbval.mean + (state->cur_line + 1) * bbval.dev*bb_multipler;
            state->lower_price = bbval.mean + (state->cur_line - 1) * bbval.dev*bb_multipler;

            state->upper = calculate_order(state, state->upper, state->upper_price, Side::sell, false);
            state->lower = calculate_order(state, state->lower, state->lower_price, Side::buy, false);
            context.run(monitor_order(state, state->upper));
            context.run(monitor_order(state, state->lower));

        } while (co_await timer.sleep_until(next_tp));
    }

    StrategyFragment reverse_position_to(PState state, Quote qt, Side side) {
        do {
            state->upper.cancel();
            state->lower.cancel();
            Order ord = calculate_order(state, {}, qt.mid(), side, true);
            OrderReport rpt;
            while (co_await ord.receive(rpt)) {
                for (auto &f: rpt.fills) {
                    process_fill(state, f);
                }
            }
            if (sgn(state->sstate.position) == static_cast<int>(side)) co_return;                      
        } while (co_await timer.sleep_for(std::chrono::minutes(1)));
        
    }

    StrategyFragment day_loop(PState state) {
        std::size_t interval = state->config.trend_detection_min;
        Timestamp next_tp;                
        Quote qt;
        do {
            next_tp = interval_upper_bound(timer.now(), std::chrono::minutes(interval));
            state->quote_stream.current(qt); //read quotes
            if (!qt.both_sides()) continue;
            
            auto ema1 = state->trend_ema.update(qt.mid());
            auto ema2 = state->trend_ema2.update(ema1);
            if (ema1 > ema2) {
                if (state->sstate.position < 0) {
                    co_await reverse_position_to(state, qt, Side::buy);                
                }
            } else if (ema1 < ema2) {
                if (state->sstate.position < 0) {
                    co_await reverse_position_to(state, qt, Side::sell);                
                }
            }

        } while (co_await timer.sleep_until(next_tp));

    }

    StrategyFragment run_instrument(TradableInstrument instrument, StrategyContext::Config config) {
 
        Quote qt;
        //wait for first quote
        auto quote_stream = instrument.subscribe<Quote>().stop_on(context.stop_signal);
        do {
            if (!co_await quote_stream.receive(qt)) co_return;
        } while (!qt.both_sides());

        auto name = instrument.get_info().name;
     
        auto state = std::make_shared<State>(State{
            {
                config["trend_detection_min"],
                config["base_interval_min"],
                config["bbema_multiplier"],
                config["reversal_multiplier"],
                config["max_loss"]
            },
            {{context.storage,std::string(name)+"_pricebb"}, config["price_bb_mean"], config["price_bb_mean"]},
            {{context.storage,std::string(name)+"_ema"}, config["trend_ema"]},
            {{context.storage,std::string(name)+"_ema2"}, config["trend_ema2"]},
            std::move(quote_stream),
            instrument,
            instrument.get_info()
        });

        auto tl = tick_loop(state).launch();
        auto dl = day_loop(state).launch();


        co_await dl;
        co_await tl;
        state->upper.cancel();
        state->lower.cancel();
        
        co_return;
    }



};



}