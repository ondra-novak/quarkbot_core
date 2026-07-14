#include "siminstrument.hpp"
#include "quarkbot/market_instrument.hpp"
#include "../streaming/snapshot_helper.hpp"

namespace quarkbot {

    awaitable<bool> SimInstrument::receive_snapshot(Snapshot &v, std::stop_token stop_token) {
        MarketInstrument me(shared_from_this());
        return receive_snapshot_from_streams(me, v, stop_token);
    }

}