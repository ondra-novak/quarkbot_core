#pragma once
#include "ta/bb_ema.hpp"
#include "ta/ema.hpp"
#include <optional>
#include <queue>
#include <vector>

class TrendingStrategyCore {
public:

    struct Config {
        std::size_t bb_interval;
        double bb_level_step;
        double multiplier;
        double min_loss;
        double max_loss;        
        bool inverse_market;
        double min_size;
        std::size_t ema_trend;

        double calc_pnl(double open, double close, double size) const;

    };

    struct Order {
        double price;
        double size;
        int lev = 0;
    };

    struct Fill {
        double price;
        double size;
        int lev;
        char src;
    };

    struct Result {
        Order buy_order;
        Order sell_order;
        double market_size;
        int market_side;
    };

    struct Input {
        double bid;
        double ask;
        std::vector<Fill> fills;
        double final_position;
    };

    TrendingStrategyCore(Config cfg);
    ///if nullopt returned, strategy needs to collect data
    std::optional<Result> operator()(const Input &input);
    

    double get_mean() const {return _bb.value().mean;}
    double get_trend_rev_price() const {return _ema.value();}
    double get_last_price() const {return _prev_price;}
    double get_position()  const {return _cur_position;}
    const Config &get_config() const {return _cfg;}

protected:
    using BB = quarkbot::Bollinger_Ema<double>;
    using EMA = quarkbot::Ema<double>;

    Config _cfg;
    BB _bb;    
    EMA _ema;
    
    int _avoid_line = 9999;
    double _cur_loss = 0;
    double _cur_position = 0.000000000000001;
    double _prev_price = 1.0;    
    double _hist = 0;
    double _prev_ema = 0;
   

    double calc_new_pos(double loss, double price) const;
    bool calculate_levels(int dir, int level, const BB::Result &bbres,  Order &best_buy, Order &best_sell, double bid, double ask);
    double calc_pnl(double price);
    double calc_new_loss(double pnl);
    double calc_position_from_pnl(double price);


};