#include "impl/mmbot_data_source.hpp"
#include "strategies/trending/trending_core.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

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


int main() {
    auto source = define_source("/home/ondra/Downloads/minute_bitfinex_BTC_USD (m).csv");

    TrendingStrategyCore::Config cfg{10000,2,0.001,
        30,30,300,
        false,0.00001, false};

    std::optional<TrendingStrategyCore::Result> orders;
    std::vector<TrendingStrategyCore::Fill> fills;
    TrendingStrategyCore strategy(cfg);

    auto v  = source();
    if (!v) return -1;
    double prev_price = *v;    
    double position = 0;
    double profit = 0;
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
        }
        double cur_profit = 0;
        std::cout << price << "," << strategy.get_position() << "," 
            << (profit +strategy.get_config().calc_pnl(strategy.get_last_price(), price, strategy.get_position()));
        for (auto &f: fills) {
            cur_profit += cfg.calc_pnl(prev_price, f.price, position);
            position += f.size;
            prev_price = f.price;        
            std::cout << "," << f.lev << "," << f.src << "," << f.price << "," << f.size;
        }
        std::cout << std::endl;
        profit += cur_profit;
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

    





}