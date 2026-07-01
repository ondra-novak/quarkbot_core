#include "../quarkbot/backtest/simexchange.hpp"
#include <quarkbot/backtest_data_source.hpp>
#include "tests/check.h"

int main() {
    auto exchange = std::make_shared<quarkbot::SimExchange>();

    quarkbot::InstrumentSpec spec;
    spec.name = "BTCUSDT";
    spec.type = quarkbot::InstrumentType::spot;
    spec.quote_currency = "USD";
    spec.pnl_currency = "USD";
    spec.asset_wallet = "BTC";
    spec.min_lot_size = Decimal(1,  -5);   // 0.00001
    spec.lot_size_increment = Decimal(1, -5);
    spec.price_increment = Decimal(1, -2);  // 0.01
    spec.min_volume = {};
    spec.leverage = {};
    spec.fee_rate_maker = Decimal(1, -3);   // 0.001
    spec.fee_rate_taker = Decimal(1, -3);
    spec.multiplier = Decimal(1);
    spec.tick_scale = Decimal(1);

    auto info = spec.resolve(*exchange);

    CHECK_EQUAL(info.name, std::string("BTCUSDT"));
    CHECK(info.type == quarkbot::InstrumentType::spot);
    CHECK_EQUAL(info.quote_currency.id, std::string("USD"));
    CHECK_EQUAL(info.pnl_currency.id, std::string("USD"));
    CHECK(info.asset_wallet.has_value());
    CHECK_EQUAL(info.asset_wallet->id, std::string("BTC"));
    CHECK(info.fee_rate_maker == Decimal(1, -3));
}
