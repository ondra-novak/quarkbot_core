#include "quarkbot/strategy_main.hpp"
#include "../strategies/mean_rev_trending/mean_rev_trending.hpp"



int main(int argc, char **argv) {
    return quarkbot::strategy_main<MeanRevTrendingStrategy>(argc, argv);
}