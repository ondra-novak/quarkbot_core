#pragma once

#include "quarkbot/abstract/ieventstream.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/order.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/persistent.hpp"
#include "quarkbot/round_policy.hpp"
#include "quarkbot/serie_persistent.hpp"
#include "quarkbot/somodule.hpp"
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
#include <memory>

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



    struct SingleInstrumentStrategy {
    public:
        //configuration

        MeanRevTrendingStrategy &owner;
        TradableInstrument instrument;
        std::size_t trend_detect_interval_min;
        std::size_t mean_rev_interval_min;
        double bbema_band_multiplier;
        double reversal_multiplier;
        double reversal_power;
        Decimal oracle_pos_fract;
        double initial_budget;
        double initial_pos_percent;
        double max_loss_percent;

        //indicators
        ta::BollingerBandsGen<ta::EMA<PersistentSerie<double> > > bbema;
        ta::EMA<PersistentSerie<double> > fast_ema;
        ta::EMA<PersistentSerie<double> > slow_ema;
      
        //state variables
        Persistent<Decimal> position;
        Persistent<Decimal> last_trade_price;
        Persistent<double> total_loss;
        Persistent<double> limiter;
        Persistent<Decimal> last_trend_check_price;
        Persistent<double> benchmark_profit;
        Persistent<double> current_profit;
        Persistent<int> center_band_index;
        Persistent<Decimal> oracle_pos;
        Persistent<double> bbupper,bblower;

        //orders
        Order upper = {};
        Order lower = {};
        Timer timer = {};

        //shared calculations
        double bbema_mean = {};
        double bbema_dev = {};

        //sync                
        StrategyFragmentGroup sync = {};

        std::pair<double, double> calculate_loss(Decimal cur_price) const {
            const auto &info = instrument.get_info();
            double pnl = info.calc_pnl(last_trade_price, cur_price, position-oracle_pos).to_double();            
            double new_loss = std::max(total_loss - pnl,0.0);
            double limit = (initial_budget + benchmark_profit) * max_loss_percent / 100.0;
            double new_limiter = new_loss < limit? 1.0:limit/new_loss;            
            return {new_loss, new_limiter};
        }    


        void process_fill(const Fill &fill) {
            current_profit = current_profit + fill.contract.calc_pnl(last_trade_price, fill.price, position).to_double();
            auto [loss, new_limiter] = calculate_loss(fill.price);
            auto pos_change = fill.quantity * static_cast<int>(fill.side) ;
            position = position + pos_change;
            last_trade_price = fill.price;
            total_loss = loss;
            limiter = new_limiter;
        }

        Decimal calc_new_pos(Decimal new_price, int side) const {
            auto [new_loss, new_limiter] = calculate_loss(new_price);            
            new_loss *= std::min(limiter.get(), new_limiter);
            return (std::sqrt(2*reversal_multiplier*new_loss + new_loss*new_loss * reversal_power*reversal_power) / new_price) *side + oracle_pos;
        }

        Decimal calc_order_diff(Decimal new_price, int side) const {
            return calc_new_pos(new_price,side) - position;
        }

        Order &select_mean_rev_order(Side side) {
            return side == Side::buy?lower:upper;
        }
        double select_mean_rev_price(Side side, int attempt) const {
//            return bbema_mean + bbema_dev * bbema_band_multiplier * (center_band_index - attempt*static_cast<int>(side));
            return last_trade_price.get().to_double() +bbema_dev * bbema_band_multiplier * (0 - attempt*static_cast<int>(side));
        }
        void recalculate_bands(Side side) {
            center_band_index = center_band_index - static_cast<int>(side);
        }

        StrategyFragment follow_mean_rev_order(Order order) {
            OrderReport rpt;
            while (co_await order.receive(rpt)) {
                for (auto &f: rpt.fills) process_fill(f);
            }
            auto side = order.get_parameters().side;
            if (rpt.status == OrderStatus::filled) {
                recalculate_bands(side);
                place_mean_rev_order_rep(side, {});
            } else if (order == select_mean_rev_order(side) && !(rpt.status == OrderStatus::rejected && rpt.rejection_reason == OrderRejectionReason::adapter_stopped)) {
                //just test, if not stopped
                if (co_await timer.sleep_for(std::chrono::seconds(10))) {
                    if (order == select_mean_rev_order(side)) {
                        place_mean_rev_order_rep( side, {});
                    }
                }
            }
        }


        bool place_mean_rev_order(Decimal new_price, Side side, Order replace_order) {
            int dir = sgn(position);
            if (dir == 0) dir = static_cast<int>(side);
            Decimal diff = calc_order_diff(new_price, dir);
            if (sgn(diff) != static_cast<int>(side)) {
                return false;
            }
            Order &cur_order = select_mean_rev_order(side);                    
            cur_order = instrument.place_order(OrderRequest{
                .label={},
                .side=side,
                .type=OrderType::limit, 
                .quantity={abs(diff), RoundStrategy::aggressive},
                .limit_price={new_price, RoundStrategy::defensive},
            }, replace_order);
                
            sync.run(follow_mean_rev_order(cur_order));
            return true;
        }

        bool place_mean_rev_order_rep( Side side, Order replace_order) {
            int a = 1;
            while (a< 1000 && !place_mean_rev_order(select_mean_rev_price(side,a), side, replace_order)) ++a;
            return a<1000;
        }

        StrategyFragment trend_detection_period(Decimal price) {
            OrderReport rpt;
            do {
                const auto &info = instrument.get_info();
                double fema = fast_ema.update(price.to_double());
                double sema = slow_ema.update(fema);
                double diff = fema - sema;
                int dir = diff >= 0?1:-1;

                Decimal ltcp = last_trend_check_price;
                if (!ltcp) ltcp = price;
                double initial_pos_cur = (initial_budget + benchmark_profit) * initial_pos_percent * 0.01;
                Decimal oracle_position = dir * initial_pos_cur / ltcp;
//              Decimal prev_pos = oracle_pos;
                oracle_pos = oracle_position*oracle_pos_fract;
//              auto prev_pnl = info.calc_pnl(ltcp, price, prev_pos).to_double();
                auto new_pnl = info.calc_pnl(ltcp, price, oracle_position).to_double();
//                auto adj_pnl = new_pnl - prev_pnl;
                benchmark_profit = (benchmark_profit + new_pnl)*limiter.get();
                total_loss = std::max(0.0,benchmark_profit.get() - current_profit.get());
                last_trend_check_price = price;

                if (dir == sgn(position.get())) {                
                    co_return;  //in direction, no need to do anything
                }


                auto quant = calc_order_diff(price, dir);
                upper.cancel();
                lower.cancel();
                upper = {};
                lower = {};
                if (quant == 0) {
                    co_return;
                }

                Order rev_order = instrument.place_order(OrderRequest{
                    .side = static_cast<Side>(sgn(quant)),
                    .type = OrderType::market,
                    .quantity = abs(quant)
                });
                while (co_await rev_order.receive(rpt)) {
                    for (auto &f: rpt.fills) {
                        process_fill(f);
                    }
                }
                if (rpt.status != OrderStatus::filled) {
                    if (!co_await timer.sleep_for(std::chrono::seconds(30))) co_return;
                }
                
            } while (rpt.status == OrderStatus::filled);
        }

        StrategyFragment quit() {
            upper.cancel();
            lower.cancel();
            timer.cancel();
            upper ={};
            lower = {};
            co_await sync.join();
        }

        StrategyFragment fast_interval(EventStream<Quote> stream) {
            auto nx = interval_upper_bound(timer.now(), std::chrono::minutes(mean_rev_interval_min));
            while (co_await(timer.sleep_until(nx))) {
                 nx = interval_upper_bound(timer.now(), std::chrono::minutes(mean_rev_interval_min));
                 Quote qt;
                 stream.current(qt);
                 double dp = qt.mid().to_double();
                 auto r = bbema.update(dp);
                 bbema_mean = r.mean;
                 bbema_dev = r.dev;
                 bbupper = select_mean_rev_price(Side::sell,1);
                 bblower = select_mean_rev_price(Side::buy, 1);
                 place_mean_rev_order_rep( Side::buy, lower);
                 place_mean_rev_order_rep( Side::sell, upper);
            }
        }

        StrategyFragment slow_interval(EventStream<Quote> stream) {
            auto nx = interval_upper_bound(timer.now(), std::chrono::minutes(trend_detect_interval_min));
            while (co_await(timer.sleep_until(nx))) {
                 nx = interval_upper_bound(timer.now(), std::chrono::minutes(trend_detect_interval_min));
                 Quote qt;
                 stream.current(qt);
                 trend_detection_period(qt.mid());
            }
        }


    };

    StrategyFragment run_instrument(TradableInstrument instrument, StrategyContext::Config cfg, PersistentNamespace ns) {
        SingleInstrumentStrategy inst {
            *this,
            instrument,
            cfg["trend_detect_interval_min"],
            cfg["mean_rev_check_interval_min"],
            cfg["bbema_band_multiplier"],
            cfg["reversal_multiplier"],
            cfg["reversal_power"],
            cfg["oracle_pos_fract"],
            cfg["initial_budget"],
            cfg["initial_pos_percent"],
            cfg["max_loss_percent"],            

            {{ns, "bbema"}, cfg["bbema_interval"], cfg["bbema_interval"]},
            {{ns,"ema_master"}, cfg["trend_indicator_interval_master"]},
            {{ns,"ema_slave"}, cfg["trend_indicator_interval_slave"]},

            {ns, "position"},
            {ns,"last_trade_price"},
            {ns,"total_loss"},
            {ns,"limiter"},
            {ns,"last_trend_check_price"},
            {ns,"benchmark_profit"},
            {ns,"current_profit"},
            {ns,"center_band_index"},
            {ns,"oracle_pos"},
            {ns,"bbupper"},
            {ns,"bblower"},
        };

        Quote qt;
        auto stream = instrument.subscribe<Quote>();
        do {
            if (!co_await stream.receive(qt)) co_return;
        } while (!qt.both_sides());

//        if (inst.last_trend_check_price.get() == 0_dec) inst.last_trend_check_price  = qt.mid();
        if (inst.last_trade_price.get() == 0_dec) inst.last_trade_price  = qt.mid();        

        auto p1 = inst.fast_interval(stream).launch();
        auto p2 = inst.slow_interval(stream).launch();
        co_await context.stop_signal;
        co_await inst.quit();
        co_await p2;
        co_await p1;        
    }


    StrategyFragment main() {

        for (auto &instrument: context.instruments) {
            auto &info = instrument.get_info();
            auto name = info.name;            
            context.run(run_instrument(std::move(instrument), 
                                 context.config/name, 
                                 {context.storage, name}));
        }
        co_return;
    }

};

}