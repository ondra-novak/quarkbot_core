#include "impl/mmbot_data_source.hpp"
#include "strategies/trending/trending_core.hpp"
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_set>

static constexpr double slippage = 0.001;

auto define_source(std::filesystem::path p) {
    return [f = std::ifstream(p), p, ln = std::string()]() mutable ->std::optional<double>{
        if (f.eof()) return {};
        if (!f) throw std::runtime_error("Failed to read file" + p.string());
        do {
            std::getline(f, ln);
            while (!ln.empty() && std::isspace(ln.back())) ln.pop_back();
            if (!ln.empty()) {
                double v = std::strtod(ln.c_str(),nullptr);
                if (v != 0.0) return v;
            }
        } while (!f.eof());
        return {};
    };
}

struct TestResult {
    double rrr = 0;
    double profit = 0;
    double turnover = 0;  
    std::vector<TrendingStrategyCore::Fill> fills;
};




TestResult run_test(TrendingStrategyCore::Config cfg, std::filesystem::path source_path, double margin, bool need_fills) {
    auto source = define_source(source_path);

/*    TrendingStrategyCore::Config cfg{600,1,
        35,40,100,
        false,0.00001,40};
    */

    std::optional<TrendingStrategyCore::Result> orders;
    std::vector<TrendingStrategyCore::Fill> fills;
    std::vector<TrendingStrategyCore::Fill> all_fills;
    TrendingStrategyCore strategy(cfg);
    double cur_down = 0;
    double cur_max = 0;
    double max_down = 0;

    auto v  = source();
    if (!v) return {};
    double prev_price = *v;    
    double position = 0;
    double profit = 0;
    double turnover = 0;
    double maxvol = margin * 10; //leverage 10
    orders = strategy({prev_price,prev_price, fills,position});
    v = source();
    while (v.has_value()) {
        double price = *v;
        if (orders) {
            if (price > orders->sell_order.price) {
                fills.push_back({ orders->sell_order.price,  -orders->sell_order.size,
                    orders->sell_order.lev,'L'});
            } 
            if (price < orders->buy_order.price) {
                fills.push_back({ orders->buy_order.price,  orders->buy_order.size,
                    orders->buy_order.lev,'L'});            
            } 
           if (price * position  > maxvol) 
                    return {-1,profit,turnover,std::move(all_fills)};
        }
        double cur_profit = 0;
        for (auto &f: fills) {
            cur_profit += cfg.calc_pnl(prev_price, f.price, position);
            position += f.size;
/*            std::cout << f.lev << "," << f.src << "," << f.price << "," << f.size
                    << "," << position << "," << (profit+cur_profit) << "," << strategy.get_trend_rev_price() << std::endl;*/
            prev_price = f.price;        
            turnover += f.price * std::abs(f.size);
            if (need_fills) all_fills.push_back(f);

        }
        profit += cur_profit;
        double upnl = profit + cfg.calc_pnl(prev_price, price, position);
        if (upnl < -margin) {
            return {-1,upnl,turnover,all_fills};
        }
        if (profit > cur_max) {
            cur_max = profit;
            max_down = std::max(cur_down, max_down);
            cur_down = 0;
        } else {
            double d = cur_max - profit;
            cur_down = std::max(d, cur_down);
        }
        orders = strategy({price,price, fills, position});
        fills.clear();
        if (orders && orders->market_side) {
            int side = orders->market_side;
            double size = orders->market_size;
            if (side > 0) {
                fills.push_back({price + price * slippage, size,0,'M'});
            } else if (side < 0) {
                fills.push_back({price - price * slippage, -size,0,'M'});
            }
            orders.reset();
        }
        v = source();
    }

    max_down = std::max(cur_down, max_down);
    double rrr = cur_max / max_down;
/*    std::cout << "Fill count: " << fill_count << std::endl;
    std::cout << "Turnover: " << std::fixed << turnover << ", fees: 0.02% (bybit): " << turnover * 0.0002 << std::endl;
    std::cout << "RRR: " << rrr << "," << cur_max << "," << max_down << std::endl;*/
    return TestResult{rrr, profit, turnover, std::move(all_fills)};
}

constexpr std::string_view src_path = "src/exec/btcusd.csv";



void test_runner(unsigned int threads) {
    std::mutex mx;
    std::priority_queue<std::pair<TrendingStrategyCore::Config, TestResult>, 
            std::vector<std::pair<TrendingStrategyCore::Config, TestResult> >,decltype([](auto &a, auto &b){
        return a.second.rrr < b.second.rrr;
    }) > explore_params;
    TestResult best_test_result{0,0,0,{}};    
    std::random_device rnddev;
    std::unordered_set<std::string> already_probed;

    std::filesystem::path source_path = src_path;
    auto source_test = define_source(source_path);

/*
    auto rand_double = [&](double min, double max){
        std::uniform_real_distribution<double> dist(min, max);
        return dist(rnddev);
    };*/

    auto rand_int = [&](int min, int max){
        std::uniform_int_distribution<int> dist(min, max);
        return dist(rnddev);
    };


    std::vector<std::jthread> thrs;

    std::cout << "rrr,profit,turnover,bb_interval,bb_level_step, ema_interval,minloss,maxloss,multiplier,best" << std::endl;
    double balance = 1000;
    int seed_interval = 20;
    int cur_seed_cycle = 0;
 

    for (unsigned int i = 0; i < threads; ++i) {
        thrs.push_back(std::jthread([&](std::stop_token tkn){
            while (!tkn.stop_requested()) {
                TrendingStrategyCore::Config cfg;
                TestResult cmpres = best_test_result; 
                cmpres.rrr/=3.0;
                {
                    std::scoped_lock _(mx);
                    if (!explore_params.empty() && cur_seed_cycle != seed_interval) {
                        const auto &c = explore_params.top();
                        cfg = c.first;
                        cmpres = c.second;
                        explore_params.pop();
                        cur_seed_cycle++;
                    } else {
                        cfg.bb_interval = static_cast<std::size_t>(rand_int(6, 100)*10);
                        cfg.bb_level_step = rand_int(1, 4)*0.5;
                        cfg.ema_trend = static_cast<std::size_t>(rand_int(1, 10));
                        cfg.ema_trend *= cfg.ema_trend;
                        cfg.min_size = 0.00001;
                        cfg.inverse_market = false;
                        cfg.min_loss = balance *0.02;
                        cfg.max_loss = balance;
                        cfg.multiplier = static_cast<double>(rand_int(10, 50));                        
                        cur_seed_cycle = 1;
                    }
                    auto s = std::string(reinterpret_cast<const char *>(&cfg), sizeof(cfg));    
                    if (!already_probed.insert(s).second) {
                        continue;
                    }
                }
                
                double margin = balance+balance*0.1;
                auto finres = run_test(cfg,source_path, margin, false);                
                std::scoped_lock _(mx);
                auto st = std::format("{:5.1f},{:6.0f},{:10.0f},{:5},{:5.1f},{:5},{:5.0f},{:5.0f},{:5.0f},{}", 
                        finres.rrr, finres.profit, finres.turnover,cfg.bb_interval, cfg.bb_level_step, 
                        cfg.ema_trend,cfg.min_loss,cfg.max_loss,cfg.multiplier,finres.rrr>best_test_result.rrr?"BEST":""
                    );   
                if (finres.rrr == -1.0) {              //liquidation
                    cfg.max_loss=cfg.max_loss-std::floor((cfg.max_loss/10));
                    if (cfg.max_loss > cfg.min_loss) {
                        explore_params.push({cfg,finres});
                    }
                }
                double prev_score = cmpres.rrr + (10000+cmpres.profit)/10000;
                double cur_score =  finres.rrr + (10000+finres.profit)/10000;
                if (std::isfinite(finres.rrr) && cur_score > prev_score && finres.rrr>3.0  && finres.profit > 0) {
                    std::cout << st << std::endl;
                    if (finres.rrr > best_test_result.rrr) best_test_result = finres;
                    auto c = cfg;

                    c.ema_trend = cfg.ema_trend+1;
                    explore_params.push({c,finres});
                    if (cfg.ema_trend > 2) {
                        c.ema_trend = cfg.ema_trend-1;
                        explore_params.push({c,finres});
                    }
                    c.ema_trend = cfg.ema_trend;

                    c.bb_interval=cfg.bb_interval+10;
                    explore_params.push({c,finres});
                    if (cfg.bb_interval>60) {
                        c.bb_interval=cfg.bb_interval-10;
                        explore_params.push({c,finres});
                    }
                    c.bb_interval=cfg.bb_interval;

                    c.bb_level_step = cfg.bb_level_step+0.1;
                    explore_params.push({c,finres});
                    if (cfg.bb_level_step > 0.1) {
                        c.bb_level_step = cfg.bb_level_step-0.1;
                        explore_params.push({c,finres});
                    }
                    c.bb_level_step = cfg.bb_level_step;


                    c.multiplier = cfg.multiplier + 1.0;
                    explore_params.push({c,finres});
                    if (cfg.multiplier > 2) {
                        c.multiplier = cfg.multiplier - 1.0;
                        explore_params.push({c,finres});
                    }
                    c.multiplier = cfg.multiplier;

                    c.max_loss=cfg.max_loss-std::floor((cfg.max_loss/10));
                    if (c.max_loss > c.min_loss) {
                        explore_params.push({c,finres});
                    }

                } else {
                    std::cerr << st << "(" << explore_params.size() << ")\r";
                }
            }
        }));
    }
    std::string ln;
    std::getline(std::cin, ln);
    for (auto &thr: thrs) thr.request_stop();

}

int main(int argc, char **argv) {
    if (argc == 1) test_runner(std::thread::hardware_concurrency())    ;
    else if (argc == 2) {
        TrendingStrategyCore::Config cfg;
        double a,b,c;
        if (sscanf(argv[1], "%lf , %lf , %lf , %lu , %lf , %lu , %lf , %lf , %lf", 
            &a,&b,&c, &cfg.bb_interval, &cfg.bb_level_step, &cfg.ema_trend, &cfg.min_loss, &cfg.max_loss, &cfg.multiplier) != 9) {
                std::cerr << "invalid format" << std::endl;
                return 1;
            }

        cfg.inverse_market =false;
        cfg.min_size =0.00001;
        auto r = run_test(cfg, src_path, 900, true);
        double pos = 0;
        double profit = 0;
        double open = 0;
        for (auto &x: r.fills) {
            profit += cfg.calc_pnl(open, x.price, pos);
            pos += x.size;
            open = x.price;
            std::cout << x.src << ","  << x.lev<<"," << x.price << "," << x.size << "," << pos << "," << profit << std::endl;
        }
    }
}