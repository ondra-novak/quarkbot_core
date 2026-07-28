#pragma once

#include "quarkbot/context.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/serie_persistent.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/timer.hpp"
#include "quarkbot/tradable_instrument.hpp"
#include "quarkbot/ta/bollinger.hpp"
#include "quarkbot/ta/ema.hpp"
#include <chrono>

namespace {
    using namespace quarkbot;

using BBEma = ta::BollingerBandsGen<ta::EMA<PersistentSerie<Decimal> > > ;
using Ema = ta::EMA<PersistentSerie<Decimal> >;

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



    struct State {
        StrategyContext::Config config;
        Ema trend_ema;               
        Decimal position = {};
        Decimal prev_size = {};
        Decimal total_loss = {};
        Decimal calc_loss = {};
        Order upper = {};
        Order lower = {};
        int cur_line = 0;
    };

    using PState = std::shared_ptr<State>;

   

    StrategyFragment run_instrument(TradableInstrument instrument, StrategyContext::Config config) {



        std::size_t base_interval = config["base_interval_min"];
        double bb_multipler = config["bbema_multiplier"];

        Quote qt;
        //wait for first quote
        auto quote_stream = instrument.subscribe<Quote>().stop_on(context.stop_signal);
        do {
            if (!co_await quote_stream.receive(qt)) co_return;
        } while (!qt.both_sides());

        
/*
        BBEma bbema = BBEma::from_period(config["bbema_period"], 
                                         config["bbema_adjust"](0.0),
                                         {qt.mid().to_double(), qt.mid().to_double()*0.001});

        auto state = std::make_shared<State>(State{config, Ema::from_period(config["trend_ema_period"], qt.mid().to_double())});

        

        Timestamp next_tp;
        do {
            next_tp = timer.now() + std::chrono::minutes(base_interval);
            quote_stream.current(qt); //read quotes
            if (!qt.both_sides()) continue;
            
            auto bbval = bbema.update(qt.mid().to_double());

            auto upper = bbval.mean + (state->cur_line + 1) * bbval.dev*bb_multipler;
            auto lower = bbval.mean + (state->cur_line - 1) * bbval.dev*bb_multipler;







        } while (co_await timer.sleep_until(next_tp));

*/

    }



};



}