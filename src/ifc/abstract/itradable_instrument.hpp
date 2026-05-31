#pragma once

#include "ifc/abstract/orderdata.hpp"
#include "ifc/order_defs.hpp"
#include "../streaming.hpp"
#include "imarket_instrument.hpp"
namespace quarkbot {



class Order;

class ITradableInstrument: public IPublisher {
public:


virtual ~ITradableInstrument() = default;
    virtual std::shared_ptr<OrderInternalData> place_order(const OrderRequest &params, std::shared_ptr<OrderInternalData> order_to_replace, 
                std::string_view name, std::size_t param_class_hash) = 0;
    virtual std::vector<Order> attach_storage(PStorage storage, std::string key_name) = 0;
    virtual bool cancel_all_orders() = 0;
    virtual PAccount get_account() const = 0;
    virtual awaitable<Position> get_position() const = 0;
    virtual PMarketInstrument get_instrument() const = 0;

    virtual OrderParameters convert_request_to_params(const OrderRequest &req, Side cur_position_side) const {
        const auto &info = get_instrument()->get_info();
        int aps = static_cast<int>(req.side);
        int aqs = req.side == cur_position_side?1:-1;
        return {
            req.side,
            req.type,
            req.quantity.get_rounded(info.quantity_increment, aqs),
            req.limit_price.get_rounded(info.price_increment, aps),
            req.stop_price.get_rounded(info.price_increment, aps),
            req.leverage,
            req.reduce_only,
            req.hedge
        };
    }
};

}