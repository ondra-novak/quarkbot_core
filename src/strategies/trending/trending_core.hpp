#pragma once
#include "ta/bb_ema.hpp"
#include <optional>
#include <vector>

class TrendingStrategyCore {
public:

    struct Config {
        std::size_t bb_interval;
        double bb_level_step;
        double hystersis;
        double multiplier;
        double target_per_minute;
        double max_loss;        
        bool inverse_market;
        double min_size;

        double calc_pnl(double open, double close, double size) const;

    };

    struct Order {
        double price;
        double size;
    };

    struct Fill {
        double price;
        double size;
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
    



protected:
    using BB = quarkbot::Bollinger_Ema<double>;

    Config _cfg;
    BB _bb;    
    double _cur_loss = 0;
    double _cur_position = 0;
    double _prev_price = 1.0;
   

    double calc_new_pos(double loss, double price) const;
    bool calculate_levels(int dir, int level, const BB::Result &bbres,  Order &best_buy, Order &best_sell, double bid, double ask);
    double calc_pnl(double price);
    double calc_new_loss(double pnl);


};