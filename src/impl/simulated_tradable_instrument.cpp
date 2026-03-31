#include "simulated_tradable_instrument.hpp" 
#include "ifc/market_instrument.hpp"
#include "ifc/order.hpp"
#include "impl/simulated_order.hpp"
#include "utils/round.hpp"
#include <memory>
#include <mutex>


namespace quarkbot {

SimulatedTradableInstrument::SimulatedTradableInstrument(std::shared_ptr<SimulatedInstrument> instrument)
:_instrument(std::move(instrument) ) {

}

static Decimal round_amount(const IMarketInstrument::Info &nfo, const Rounded &amount) {
    Decimal f1(amount.get_rounded(nfo.lot_size_increment.to_double(), -1),nfo.min_lot_size.precision());
    if (f1 < nfo.min_lot_size) {
        f1 = Decimal (amount.get_rounded(nfo.min_lot_size.to_double(), -1),nfo.min_lot_size.precision());
    }
    return f1;
}

static Decimal round_price(const IMarketInstrument::Info &nfo, const Rounded &price, Side side) {
    int def = -static_cast<int>(side);
    return Decimal(price.get_rounded(nfo.price_increment.to_double(), def), nfo.price_increment.precision());
}


POrder SimulatedTradableInstrument::place_order(const OrderRequest &req, POrder order_to_replace, std::string_view name) {
    std::lock_guard _(_pos_mx);
    const Info nfo = get_info();
    OrderParameters params {
        req.side,
        req.type,
        round_amount(nfo, req.amount),
        round_price(nfo, req.limit_price, req.side),
        round_price(nfo, req.stop_price, req.side),
        round_price(nfo,req.trailing_offset, req.side),
        req.leverage,
        req.reduce_only,
        req.hedge        
    };

    auto order = std::make_shared<SimulatedOrder>(params, shared_from_this(), order_to_replace, name);

    if (order_to_replace) {
        auto sim_replace = std::dynamic_pointer_cast<SimulatedOrder>(order_to_replace);
        if (sim_replace && sim_replace->get_instrument().get() == this) {
            const auto &old_params = sim_replace->get_parameters();
            if (old_params.side == params.side && old_params.type == params.type) {
                _instrument->place_order(order);
                return order;
            }
        }
        order->reject(OrderRejectionReason::invalid_replace);
        return order;
    }


    _instrument->place_order(order);
    return order;
    
    
}
void SimulatedTradableInstrument::attach_storage(PStorage storage, function_view<void(POrder)> callback) {

}
coro::awaitable<IStorage::TradingState> SimulatedTradableInstrument::aggregate_fills(PStorage fill_storage, IStorage::TradingState current_state,
                                                    std::chrono::system_clock::time_point until_time) 
{

}
coro::awaitable<IStorage::FeeState> SimulatedTradableInstrument::aggregate_fees(PStorage fill_storage, IStorage::FeeState initial_state, std::chrono::system_clock::time_point until_time) {

}
coro::awaitable<std::span<const Fill> > SimulatedTradableInstrument::get_last_fills(std::span<Fill> space) {

}
PAccount SimulatedTradableInstrument::get_account() const {

}
coro::awaitable<Decimal> SimulatedTradableInstrument::get_position() const {
    std::lock_guard _(_pos_mx);
    return _position;
}
void SimulatedTradableInstrument::update_position(Decimal amount) {
    std::lock_guard _(_pos_mx);
    _position+=amount;            
}

SimulatedTradableInstrument::RiskLimits SimulatedTradableInstrument::get_limits() const {

}




}