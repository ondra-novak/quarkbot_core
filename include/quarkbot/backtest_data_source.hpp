#pragma once

#include "quarkbot/abstract/imarket_instrument.hpp"
#include "quarkbot/types.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/stream/trade.hpp"
#include "quarkbot/stream/orderbook.hpp"
#include "decimal.hpp"
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace quarkbot {

class IExchange;

struct InstrumentSpec {
    std::string name;
    InstrumentType type = InstrumentType::spot;
    std::string quote_currency;
    std::string pnl_currency;
    std::optional<std::string> asset_wallet;
    Decimal min_lot_size = {};
    Decimal lot_size_increment = {};
    Decimal price_increment = {};
    Decimal min_volume = {};
    Decimal leverage = {};
    Decimal fee_rate_maker = {};
    Decimal fee_rate_taker = {};
    Decimal multiplier = Decimal(1);
    Decimal tick_scale = Decimal(1);

    IMarketInstrument::Info resolve(IExchange &exchange) const;
};

class IBacktestDataSource {
public:
    using EventData = std::variant<Quote, Trade, OrderBookIncrement>;
    struct Event {
        std::chrono::system_clock::time_point time;
        std::string instrument;
        EventData payload;
    };

    virtual std::optional<Event> next_event() = 0;
    virtual std::vector<InstrumentSpec> get_instrument_infos() { return {}; }
    virtual ~IBacktestDataSource() = default;
};

} // namespace quarkbot
