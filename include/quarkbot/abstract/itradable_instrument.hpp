#pragma once

#include "../defs.hpp"
#include "../order_defs.hpp"
#include "../utils/function_view.hpp"
#include "ipublisher.hpp"
namespace quarkbot {



class Order;

class ITradableInstrument: public IPublisher {
public:

    struct StorageConfig {        
        ///keys to store fills
        std::string fills_key;
        ///keys to store trade stats
        std::string trades_key;
        ///keys to store active orders
        std::string active_orders_key;
    };
    virtual ~ITradableInstrument() = default;

    virtual POrder place_order(const OrderRequest &params, POrder order_to_replace, std::size_t param_class_hash) = 0;
    virtual bool attach_storage(PStorage storage, StorageConfig cfg, function_view<void(const Order &)> order_callback) = 0;
    virtual bool cancel_all_orders() = 0;
    virtual PAccount get_account() const = 0;
    virtual awaitable<Position> get_position() const = 0;
    virtual PMarketInstrument get_instrument() const = 0;

    virtual OrderParameters convert_request_to_params(const OrderRequest &req, Side cur_position_side) const = 0;

    class Null;
};

class ITradableInstrument::Null final: public ITradableInstrument {
public:
    virtual POrder place_order(const OrderRequest &, POrder , std::size_t ) {
        throw UninitializedException();
    }
    virtual bool attach_storage(PStorage , StorageConfig , function_view<void(const Order &)> ) {
        return false;
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
    virtual OrderParameters convert_request_to_params(const OrderRequest &, Side ) const {return {};}
};


}