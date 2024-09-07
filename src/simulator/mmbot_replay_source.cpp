/*
 * mmbot_replay_source.cpp
 *
 *  Created on: 6. 9. 2024
 *      Author: ondra
 */

#include "mmbot_replay_source.h"
#include <fstream>
#include <random>
namespace quarkbot {

namespace Replay {

class MMBotReplaySource {
public:

    MMBotReplaySource(std::istream &src,
            std::chrono::system_clock::time_point initial_time,
            MMBotSourceConfig cfg)
        :_src(src), _initial_time(initial_time), _cfg(std::move(cfg))
        ,_rnd(cfg.seed)
        ,_volume_dist(cfg.min_volume, cfg.max_volume)
        ,_spread_dist(0.0, 0.25)
        ,_event_dist(cfg.market_events_per_minute)
    {
        _data.symbol_id = _cfg.symbol;
        _src >> _cur_price;
    }

    const Data *operator()();

protected:

    std::istream &_src;
    std::chrono::system_clock::time_point _initial_time;
    MMBotSourceConfig _cfg;
    std::default_random_engine _rnd;
    std::uniform_real_distribution<double> _volume_dist;
    std::normal_distribution<double> _spread_dist;
    std::exponential_distribution<double> _event_dist;
    Data _data;
    double _fracpos = 1.0;
    double _prev_price = 0.0;
    double _cur_price = 0.0;
    std::size_t _counter = 0;


    void generate_spread(double pos);
};



Source create(const std::string &fname, std::chrono::system_clock::time_point initial_time, MMBotSourceConfig config) {
    auto src = std::make_unique<std::ifstream>(fname);
    if (!(*src)) throw std::runtime_error("Can't open file (mmbot price file):" + fname);
    std::istream &srcref = *src;
    return [src = std::move(src), parser = MMBotReplaySource(srcref, initial_time, std::move(config))]() mutable {
        return parser();
    };
}
Source create(std::istream &src, std::chrono::system_clock::time_point initial_time,  MMBotSourceConfig config) {
    return MMBotReplaySource(src, initial_time, std::move(config));
}

const Data* MMBotReplaySource::operator ()() {
    if (_src.eof()) return nullptr;
    if (_fracpos>=1.0) {
        ++_counter;
        _data.cum_trades = _counter;
        _data.last = Decimal(_cur_price);
        _data.cum_volume+=_volume_dist(_rnd);
        generate_spread(1.0);
        _prev_price = _cur_price;
        _src >> _cur_price;
        _fracpos = 0.0;
    } else {
        generate_spread(_fracpos);
        double nx = _event_dist(_rnd);
        _fracpos += nx;
    }
    double second_pos = ((_counter + _fracpos) * 60.0  + _cfg.offset) * _cfg.speed;
    auto absdur = std::chrono::microseconds(static_cast<std::uint64_t>(second_pos*1000000));
    _data.tp = absdur+_initial_time;
    return &_data;
}


void MMBotReplaySource::generate_spread(double pos) {
    double ask_dev = std::exp(_spread_dist(_rnd));
    double bid_dev = std::exp(_spread_dist(_rnd));
    double ask_vol = _volume_dist(_rnd);
    double bid_vol = _volume_dist(_rnd);
    double price = _prev_price + (_cur_price - _prev_price) * pos;
    _data.ask = Decimal(price * std::exp(ask_dev*_cfg.spread_percent*0.01));
    _data.bid = Decimal(price * std::exp(-bid_dev*_cfg.spread_percent*0.01));
    _data.ask_volume = ask_vol;
    _data.bid_volume = bid_vol;
    _data.index = _cur_price;

}

}

}

