#pragma once

#include "ifc/defs.hpp"
#include "simulated_instrument.hpp"
#include "ifc/tradable_instrument.hpp"
#include "utils/spin_mutex.hpp"
#include <memory>
#include <mutex>
namespace quarkbot {


    class SimulatedTradableInstrument : public ITradableInstrument, public std::enable_shared_from_this<SimulatedTradableInstrument> {
    public:

        SimulatedTradableInstrument(std::shared_ptr<SimulatedInstrument> instrument);
        virtual PUnderlyingCurrency get_quote_currency() const override{
            return _instrument->get_quote_currency();
        }
        virtual PUnderlyingCurrency get_asset() const override{
            return _instrument->get_asset();
        }
        virtual PUnderlyingCurrency get_pnl_currency() const override{
            return _instrument->get_pnl_currency();
        }
        virtual PExchange get_exchange() const override{
            return _instrument->get_exchange();
        }
        virtual Info get_info() const override{
            return _instrument->get_info();
        }
        virtual std::shared_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type) const override{
            return _instrument->subscribe_stream_internal(type);            
        }
        virtual awaitable<PTradableInstrument> create_tradable_instrument(PAccount account) const override{
            if (account == _account) return const_cast<SimulatedTradableInstrument *>(this)->shared_from_this();
            return _instrument->create_tradable_instrument(std::move(account));
        }
        virtual std::string_view get_name() const override{
            return _instrument->get_name();
        }
        virtual POrder place_order(const OrderRequest &params, POrder order_to_replace = {}, std::string_view name = {}) override;
        virtual void attach_storage(PStorage storage, function_view<void(POrder)> callback) override;
        virtual coro::awaitable<IStorage::TradingState> aggregate_fills(PStorage fill_storage, IStorage::TradingState current_state = {},
                                                    std::chrono::system_clock::time_point until_time = std::chrono::system_clock::time_point::max()) override;
        virtual coro::awaitable<IStorage::FeeState> aggregate_fees(PStorage fill_storage, IStorage::FeeState initial_state = {}, std::chrono::system_clock::time_point until_time = std::chrono::system_clock::time_point::max()) override;
        virtual coro::awaitable<std::span<const Fill> > get_last_fills(std::span<Fill> space) override;
        virtual PAccount get_account() const override;
        virtual coro::awaitable<Decimal> get_position() const override;
        virtual RiskLimits get_limits() const override;
        

        void update_position(Decimal amount);
        std::shared_ptr<SimulatedInstrument> get_origin_instrument() const {return _instrument;}


    protected:
        std::shared_ptr<SimulatedInstrument> _instrument;
        Decimal _position;
        mutable spin_mutex _pos_mx;
        PAccount _account;
        PStorage _storage;

    };


}
