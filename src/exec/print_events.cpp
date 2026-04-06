
#include "ifc/types.hpp"
#include "strategies/print_events/print_events.hpp"
#include "impl/backtest.hpp"
#include "impl/mmbot_data_source.hpp"
#include <chrono>
#include <memory>
using namespace quarkbot;

int main() {
    auto source = std::make_shared<MMBOT_backtest_datasource>("BTCUSD", "/home/ondra/Stažené/minute_bitfinex_BTC_USD (m) (1).csv",
        std::chrono::system_clock::now()-std::chrono::days(365));

    std::array<std::pair<std::string, Decimal>,1 > wallet{std::pair<std::string, Decimal>("USD",1000_dec)};
    Backtest bt(source, "backtest", wallet);
    auto usd = bt.get_exchange().create_currency("USD");
    bt.add_instrument({
        {},0.00001_dec, 0.00001_dec,1, 10,10,0,0,usd,usd,{},"BTCUSD"        
    });
    bt.run(print_events(bt.get_context()));
}