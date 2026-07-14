#pragma once

#include "orderdata.hpp"
#include "../order_defs.hpp"
#include "imarket_instrument.hpp"
#include "quarkbot/types.hpp"
#include <stdexcept>
namespace quarkbot {



class Order;

class ITradableInstrument: public IPublisher {
public:


    virtual ~ITradableInstrument() = default;
    virtual std::shared_ptr<OrderInternalData> place_order(const OrderRequest &params, std::shared_ptr<OrderInternalData> order_to_replace, std::size_t param_class_hash) = 0;
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
    }

    class Null;
};

class ITradableInstrument::Null final: public ITradableInstrument {
public:
    virtual std::shared_ptr<OrderInternalData> place_order(const OrderRequest &, std::shared_ptr<OrderInternalData> , std::size_t ) {
        throw UninitializedException();
    }
    virtual std::vector<Order> attach_storage(PStorage , std::string ){
        throw UninitializedException();
    }
    virtual bool cancel_all_orders() {
        return false;
    };
    virtual PAccount get_account() const {
        throw UninitializedException();
    }
    virtual awaitable<Position> get_position() const {
        return Position{};
    }
    virtual PMarketInstrument get_instrument() const {
        throw UninitializedException();
    }
    virtual std::shared_ptr<IEventStreamBase> subscribe_stream(std::size_t , const void *) {
        return {};
    }
};

constexpr auto null_tradable_instrument = ITradableInstrument::Null{};

}