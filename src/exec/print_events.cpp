
#include "quarkbot/backtest/backtest.hpp"
#include "quarkbot/backtest/simexchange.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/instrument_description.hpp"
#include "quarkbot/log.hpp"
#include "quarkbot/backtest/minute_data_source.hpp"
#include "../strategies/print_events/print_events.hpp"
#include <string_view>
using namespace quarkbot;

constexpr std::string_view source_data = "83421\n"
"83425\n"
"83125\n"
"83251\n"
"83312\n"
"83111\n"
"82825\n"
"82525\n"
"82451\n"
"82312\n"
"82211\n"
"82325\n"
"82125\n"
"82351\n"
"82212\n"
"82411\n";

int main() {

    auto source_file = std::istringstream(std::string(source_data));
    MinuteDataSource source("BTCUSD", source_file, std::chrono::system_clock::now());

    std::array<std::pair<std::string, Decimal>,1 > wallet{std::pair<std::string, Decimal>("USD",1000_dec)};
    std::array<InstrumentDescription,1> instruments = {InstrumentDescription{
        {},
        {0.00001_dec, Decimal::max(), 0.00001_dec,1, 10,10,0,0},
        {"USD"},
        {"USD"},{},"BTCUSD",InstrumentCategory::Crypto}
    };

    BacktestEnv bt("backtest",wallet,instruments,{});
    bt.add_strategy<PrintEventStrategy>({}, "example argyment");
    bt.run(std::ref(source));

}