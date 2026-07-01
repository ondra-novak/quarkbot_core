#include "trending_core.hpp"
#include <iostream>
#include <limits>


static inline double sgn(double pos) {
    return pos <0?-1:pos>0?1:0;
}


double TrendingStrategyCore::Config::calc_pnl(double open, double close, double size) const {
        if (inverse_market) {
            return  (1/open - 1/close) * size;
        } else {
            return (close - open) * size;
        }

}

TrendingStrategyCore::TrendingStrategyCore(Config cfg)
    :_cfg(cfg)
    ,_bb(_bb.from_period(cfg.bb_interval, 0, {}))
    ,_ema(_ema.from_period(_cfg.ema_trend, 0)) {
    }

    
std::optional<TrendingStrategyCore::Result> TrendingStrategyCore::operator()(const Input &input) {
    double price = (input.ask + input.bid)*0.5;
    if (_bb.value().mean == 0) {
        _bb = _bb.from_period(_cfg.bb_interval, 0, {price, price *0.01});
        _ema = _ema.from_period(_cfg.ema_trend, price);
        _prev_ema = price;
        _bb.update(price);
        return {};
    }
    auto bbres = _bb.update(price);

    

    for (auto &f: input.fills) {
        double pnl = calc_pnl(f.price);
        _cur_loss = std::max(0.0,_cur_loss - pnl);
        _cur_position += f.size;        
        _prev_price = f.price;
        _avoid_line = f.lev;
        if (&f == &input.fills[0]) {
            _prev_ema = _ema.value();
            _ema.update(f.price);
        }
    }
    _cur_position = input.final_position;

    auto newloss = calc_new_loss(calc_pnl(price));
    if (newloss > _cfg.max_loss && newloss >= _cur_loss) {
        _cur_loss = newloss;
        _prev_price = price;
    }
    

    Order best_buy = {0,std::numeric_limits<double>::max()};
    Order best_sell = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    int lev = 0;
    while (calculate_levels(1,lev, bbres, best_buy, best_sell, input.bid, input.ask) && lev < 100) lev++;
    lev = -1;
    while (calculate_levels(-1, lev, bbres, best_buy, best_sell, input.bid, input.ask) && lev > -100) lev--;
    

/*
    _hist = _cfg.hystersis;
    
    double buy_hyst = _ema.value() *(1.0+_hist);
    double sell_hyst = _ema.value() *(1.0-_hist);
    if (_cur_position >= 0 && input.ask < sell_hyst) {
        double p =  _cur_position+calc_new_pos(_cur_loss - calc_pnl(input.ask),input.ask) ;;
        if (_cfg.no_market_order) {        
            res.sell_order.price = input.ask;
            res.sell_order.size = p;
            res.sell_order.lev = 999;        
        } else {
            res.market_size = p;
            res.market_side = res.market_size >= _cfg.min_size?-1:0;
        }
    } else if (_cur_position <= 0 && input.bid > buy_hyst) { 
        double p=  calc_new_pos(_cur_loss - calc_pnl(input.bid),input.bid) - _cur_position;
        if (_cfg.no_market_order) {        
            res.buy_order.price = input.bid;
            res.buy_order.size = p;
            res.buy_order.lev = 999;
        } else {
            res.market_size = p;
            res.market_side = res.market_size >= _cfg.min_size?1:0;
        }
    } 
    
  */
    double market_size = 0;
    int market_side = 0;

    if (best_sell.price < _ema.value() && _cur_position > 0) {
        best_sell.size += 2*_cur_position;
        best_buy.size = 0;
        _avoid_line = 9999;
/*        if (price < best_sell.price - 2*bbres.dev) {
            market_size = best_sell.size;
            market_side = -1;     
        }*/
    }
    if (best_buy.price > _ema.value() && _cur_position < 0) {
        best_buy.size += -2*_cur_position;
        best_sell.size = 0;
        _avoid_line = 9999;
        /*
        if (price > best_buy.price - 2*bbres.dev) {
            market_size = best_buy.size;
            market_side = 1;     
        }
            */
    }

    Result res {best_buy, best_sell,market_size,market_side};


    return res;
}

double TrendingStrategyCore::calc_new_pos(double loss, double price) const {
    double my_loss = std::min(_cfg.max_loss, std::max(_cfg.min_loss,loss));
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

double TrendingStrategyCore::calc_position_from_pnl(double price) {
    double pnl = calc_pnl(price);
    double new_loss = calc_new_loss(pnl);
    double pos1 = calc_new_pos(new_loss, price);
    double pos2 = calc_new_pos(std::max(_cfg.min_loss,_cfg.max_loss-pnl), price);
    return std::min(pos1, pos2);
}

bool TrendingStrategyCore::calculate_levels(int dir, int level,const BB::Result &bbres, Order &best_buy, Order &best_sell, double bid, double ask) {
    if (level == _avoid_line) 
        return true;
    double price = bbres.mean + bbres.dev * _cfg.bb_level_step * level;
    double s= sgn(_cur_position);
    if (s == 0) s = sgn(_prev_price - price);
    double pos = calc_position_from_pnl(price)*s;
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
        double diff = _cur_position - pos;
        if (diff < _cfg.min_size) return true;
        if (best_sell.size > diff) {
            best_sell.size = diff;
            best_sell.price = price;
            best_sell.lev = level;
        }
        return dir != 1;        
    }
    return true;
}

