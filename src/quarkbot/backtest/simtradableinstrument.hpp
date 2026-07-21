#pragma once

#include "quarkbot/order_defs.hpp"
#include "simexecutor.hpp"
#include "siminstrument.hpp"
#include "../streaming/queue_event_stream.hpp"

#include "../common/orderdata.hpp"
#include "../common/tradable_instrument_base.hpp"
#include <quarkbot/defs.hpp>
#include <quarkbot/order.hpp>
#include <quarkbot/tradable_instrument.hpp>
#include <quarkbot/types.hpp>
#include <quarkbot/decimal.hpp>
#include <quarkbot/abstract/itradable_instrument.hpp>
#include <quarkbot/stream/external_fill.hpp>

#include <memory>
#include <optional>


namespace quarkbot {

class SimInstrument;
class SimAccount;


class SimTradableInstrument final: public TradableInstrumentBase {
public:
    SimTradableInstrument(std::shared_ptr<SimInstrument> instr, std::shared_ptr<SimAccount> account)
        :TradableInstrumentBase(std::move(instr), std::move(account)) {}


    void report_fill(const Fill &fill);    
    void report_price(Decimal price) ;


    
    virtual std::shared_ptr<IEventStreamBase> subscribe_stream(std::size_t class_hash, const void *params) override ;
    virtual bool cancel_all_orders() override;
    virtual awaitable<Position> get_position() const override {return _position;}

    void on_order_update(POrder ord, OrderStatusUpdate &&status);
    auto get_sim_account() const {return std::static_pointer_cast<SimAccount>(_account);}
    auto get_sim_instrument() const {return std::static_pointer_cast<SimInstrument>(_instrument);}

    virtual POrderData create_order(const OrderParameters &params, POrder replaced_order, std::size_t class_hash) override;
    virtual void submit_order(POrderData order) override;
    virtual bool need_local_trigger(OrderType type) const override;

protected:

    struct RegOrder {
    void on_order_update(POrder ord, OrderStatusUpdate &&status);
        POrder order;
        Decimal turnover;        
        Decimal filled = {};
    };

    std::vector<RegOrder> _active_orders;
    
    

    Position _position = {}; //current position
    Decimal _position_blocked = {};
    Decimal _margin;   //margin for position and orders
    Decimal _last_price = {};
    Decimal _upnl = {};

    std::weak_ptr<QueueEventPublisher<ExternalFill> > _liquidation_stream;

    bool update_margin() {
        const auto &info = _instrument->get_info();
        bool ok = true;
        if (info.is_leveraged()) {
            Decimal buy_orders = {};
            Decimal sell_orders = {};
            Decimal position_turnover = _position.get_volume(info);
            for (auto &[o, t,f]: _active_orders) {
                const auto &params = o->get_parameters();
                auto to = calc_turnover(params, o->get_instrument(), _last_price, f);
                if (params.side == _position.side) {
                    to = to - position_turnover;
                }
                 if (to>0) {
                    if (params.side == Side::buy) buy_orders += to;
                    else if (params.side == Side::sell) sell_orders += to;                    
                }
            }
            Decimal bigger = std::max(buy_orders, sell_orders);
            Decimal margin = bigger / info.leverage;
            ok = get_sim_account()->update_wallet(info.pnl_currency, [&](SimAccount::WalletInfoExt &w){
                w.initial_margin  = margin - _margin;
            }, true) || ok;
            _margin = margin;
        } else {
            Decimal cur_blocked = {};
            Decimal pos_blocked = {};
            for (auto &[o, t,f]: _active_orders) {
                const auto &params = o->get_parameters();
                if (params.side == Side::buy) {
                    auto to = calc_turnover(params, o->get_instrument(),_last_price, f);
                    cur_blocked+=to;
                } else {
                    pos_blocked += std::max<Decimal>(params.quantity - f,0);                    
                }
            }
            ok = get_sim_account()->update_wallet(info.quote_currency, [&](SimAccount::WalletInfoExt &w){
                w.order_blocked = cur_blocked - _margin;
            }, true) || ok;
            _margin = cur_blocked;
            if (info.asset_has_wallet()) {
                ok = get_sim_account()->update_wallet(*info.asset_wallet, [&](SimAccount::WalletInfoExt &w){
                    w.order_blocked = pos_blocked - _position_blocked;
                }, true) || ok;
            }
            _position_blocked = pos_blocked;
        }
        return ok;
    }


    void liquidation();
    POrder liquidation_order; 
    void finish_order(POrderData ord);
    
    

};

}
