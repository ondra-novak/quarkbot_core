#include "quarkbot/backtest/cli.hpp"
#include "../strategies/mean_rev_trending/mean_rev_trending.hpp"




int main(int argc, char **argv) {
    return quarkbot::start<MeanRevTrendingStrategy>(argc, argv);
}