#pragma once

#include "impl/base_order.hpp"
#include "impl/simulated_instrument.hpp"
#include "impl/simulated_tradable_instrument.hpp"
#include <memory>
namespace quarkbot {


class SimulatedOrder : public BaseOrder, public std::enable_shared_from_this<SimulatedOrder> {
public:

    SimulatedOrder(const OrderParameters &params,
             std::shared_ptr<SimulatedTradableInstrument> instrument,
             POrder replaced,
             std::string_view name)
                : BaseOrder(params, instrument, replaced, name) {}

    using BaseOrder::post_update;
    coro::prepared_coro post_update(Fill fill)  {
        auto instr = this->get_instrument();
        auto sim = std::static_pointer_cast<SimulatedTradableInstrument>(instr);
        if (sim) {
            sim->update_position(fill.amount);
        }
        return BaseOrder::post_update(fill);
    }
    virtual void cancel() {
        auto instr = this->get_instrument();
        auto tsim = std::static_pointer_cast<SimulatedTradableInstrument>(instr);
        if (tsim) {
            auto sim = tsim->get_origin_instrument();
            sim->cancel_order(shared_from_this());
        }
    }
protected:

};
}