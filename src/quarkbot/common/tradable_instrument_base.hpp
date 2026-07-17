#pragma once

#include "quarkbot/abstract/itradable_instrument.hpp"
#include "quarkbot/abstract/imarket_instrument.hpp"

namespace quarkbot {

    class TradableInstrumentBase: public ITradableInstrument {
    public:

        virtual OrderParameters convert_request_to_params(const OrderRequest &req, Side cur_position_side) const {
            const auto &info = get_instrument()->get_info();
            int aps = static_cast<int>(req.side);
            int aqs = req.side == cur_position_side?1:-1;
            return {
                req.label,
                req.side,
                req.type,
                req.quantity.get_rounded(info.quantity_increment, aqs),
                req.limit_price.get_rounded(info.price_increment, aps),
                req.stop_price.get_rounded(info.price_increment, aps),
                req.time_in_force,
                req.leverage,
                req.reduce_only,
                req.hedge,
                req.local_trigger,
                req.keep_alive,
                req.reason_override
            };
        };
    };

}