#include "trending_core.hpp"
#include "ta/bb_ema.hpp"
#include <iostream>
#include <limits>

double TrendingStrategyCore::Config::calc_pnl(double open, double close, double size) const {
        if (inverse_market) {
            return  (1/open - 1/close) * size;
        } else {
            return (close - open) * size;
        }

}

TrendingStrategyCore::TrendingStrategyCore(Config cfg)
    :_cfg(cfg)
    ,_bb(_bb.from_period(cfg.bb_interval, 0, {})) {
        _cur_loss = _cfg.target_per_minute * 1440;
    }

    
std::optional<TrendingStrategyCore::Result> TrendingStrategyCore::operator()(const Input &input) {
    double price = (input.ask + input.bid)*0.5;
    if (_bb.value().mean == 0) {
        _bb = _bb.from_period(_cfg.bb_interval, 0, {price, price *0.01});
        _bb.update(price);
        return {};
    }
    auto bbres = _bb.value();

    _next_target += _cfg.target_per_minute;

    for (auto &f: input.fills) {
        double pnl = calc_pnl(f.price);
        _cur_loss = std::max(0.0,_cur_loss - pnl);
        _cur_position += f.size;        
        _prev_price = f.price;
        _cur_loss += _next_target;
        _next_target = 0.0;
    }
    _cur_position = input.final_position;
    Order best_buy = {0,std::numeric_limits<double>::max()};
    Order best_sell = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    int lev = 0;
    while (calculate_levels(1,lev, bbres, best_buy, best_sell, input.bid, input.ask) && lev < 100) lev++;
    lev = -1;
    while (calculate_levels(-1, lev, bbres, best_buy, best_sell, input.bid, input.ask) && lev > -100) lev--;
    
    Result res {best_buy, best_sell,0,0};

    double buy_hyst = bbres.mean * std::exp(_cfg.hystersis);
    double sell_hyst = bbres.mean * std::exp(-_cfg.hystersis);
    if (_cur_position >= 0 && input.ask < sell_hyst) {
        res.market_size = _cur_position+calc_new_pos(_cur_loss - calc_pnl(input.ask),input.ask) ;
        res.market_side = res.market_size >= _cfg.min_size?-1:0;
    } else if (_cur_position <= 0 && input.bid > buy_hyst) { 
        res.market_size = calc_new_pos(_cur_loss - calc_pnl(input.bid),input.bid) - _cur_position;
        res.market_side = res.market_size >= _cfg.min_size?1:0;
    }
    
    _bb.update(price);
    return res;
}

double TrendingStrategyCore::calc_new_pos(double loss, double price) const {
    double my_loss = std::min(_cfg.max_loss, loss);
    if (_cfg.inverse_market) {
        return my_loss * _cfg.multiplier * price;
    } else {
        return my_loss * _cfg.multiplier / price;
    }
}


double TrendingStrategyCore::calc_pnl(double price) {
    return _cfg.calc_pnl(_prev_price, price, _cur_position);
}

double TrendingStrategyCore::calc_new_loss(double pnl) {
    return std::max(_cur_loss - pnl,0.0);
}

static inline double sgn(double pos) {
    return pos <0?-1:pos>0?1:0;
}

bool TrendingStrategyCore::calculate_levels(int dir, int level,const BB::Result &bbres, Order &best_buy, Order &best_sell, double bid, double ask) {
    double price = bbres.mean + bbres.dev * _cfg.bb_level_step * level;
    double s= sgn(_cur_position);
    if (s == 0) s = sgn(_prev_price - price);
    double pos = calc_new_pos(calc_new_loss(calc_pnl(price)),price) * s;
    if (price < bid) {        
        double diff = pos - _cur_position;
        if (diff < _cfg.min_size) return true;
        if (best_buy.size > diff) {
            best_buy.size = diff;
            best_buy.price = price;
            best_buy.lev = level;
        }
        return dir != -1;
    } 
    if (price > ask) {
        double diff = pos - _cur_position;
        if (diff > -_cfg.min_size) return true;
        if (best_sell.size > -diff) {
            best_sell.size = -diff;
            best_sell.price = price;
            best_sell.lev = level;
        }
        return dir != 1;        
    }
    return true;
}

