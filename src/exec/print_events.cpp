
#include "ifc/types.hpp"
#include "strategies/print_events/print_events.hpp"
#include "impl/backtest.hpp"
#include "impl/mmbot_data_source.hpp"
#include <chrono>
#include <memory>
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
    auto source = std::make_shared<MMBOT_backtest_datasource>("BTCUSD", source_file, std::chrono::system_clock::now());

/*    auto source = std::make_shared<MMBOT_backtest_datasource>("BTCUSD", "/home/ondra/Stažené/minute_bitfinex_BTC_USD (m) (1).csv",
        std::chrono::system_clock::now()-std::chrono::days(365));
        */

    std::array<std::pair<std::string, Decimal>,1 > wallet{std::pair<std::string, Decimal>("USD",1000_dec)};
    Backtest bt(source, "backtest", wallet);
    auto usd = bt.get_exchange().create_currency("USD");
    bt.add_instrument({
        {},{0.00001_dec, Decimal::max(), 0.00001_dec,1, 10,10,0,0},usd,usd,{},"BTCUSD"        
    });
    PrintEventStrategy s(bt.get_context());
    bt.run(s.main());

}