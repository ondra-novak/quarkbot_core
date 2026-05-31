#pragma once

#include "ifc/abstract/orderdata.hpp"
#include "ifc/defs.hpp"
#include "ifc/order.hpp"
#include "ifc/order_storage.hpp"
#include "ifc/tradable_instrument.hpp"
#include "impl/streaming/queue_event_stream.hpp"
#include "siminstrument.hpp"
#include "ifc/types.hpp"
#include "simexecutor.hpp"
#include "utils/decimal.hpp"
#include "ifc/abstract/itradable_instrument.hpp"
#include "ifc/stream/external_fill.hpp"

#include <memory>
#include <optional>


namespace quarkbot {

class SimInstrument;
class SimAccount;


class SimTradableInstrument final: public ITradableInstrument, public std::enable_shared_from_this<SimTradableInstrument> {
public:
    SimTradableInstrument(std::shared_ptr<SimInstrument> instr, std::shared_ptr<SimAccount> account)
        :_instrument(std::move(instr)), _account(std::move(account)) {}


    void report_fill(const Fill &fill);    
    void report_price(Decimal price) ;


    auto get_sim_instrument() const {
        return _instrument;
    }
     
    
    virtual std::unique_ptr<IEventStreamBase> subscribe_stream(std::size_t class_hash, const void *params) override ;
    virtual PAccount get_account() const override ;
    virtual PMarketInstrument get_instrument() const override ;
    virtual std::shared_ptr<OrderInternalData> place_order(const OrderRequest &params, std::shared_ptr<OrderInternalData> replaced , std::string_view name, std::size_t) override;
    virtual bool cancel_all_orders() override;
    virtual awaitable<Position> get_position() const override {return _position;}
    virtual std::vector<Order> attach_storage(PStorage storage, std::string key_name) override;

    void on_order_update(POrderAData ord, OrderInternalData::Update &&status);

protected:

    struct RegOrder {
        POrderAData order;
        Decimal turnover;        
        Decimal filled = {};
    };

    std::shared_ptr<SimInstrument> _instrument;
    std::shared_ptr<SimAccount> _account;   
    std::vector<RegOrder> _active_orders;
    std::shared_ptr<OrderStorage> _order_storage;
    

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
            ok = _account->update_wallet(info.pnl_currency, [&](SimAccount::WalletInfoExt &w){
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
            ok = _account->update_wallet(info.quote_currency, [&](SimAccount::WalletInfoExt &w){
                w.order_blocked = cur_blocked - _margin;
            }, true) || ok;
            _margin = cur_blocked;
            if (info.asset_has_wallet()) {
                ok = _account->update_wallet(*info.asset_wallet, [&](SimAccount::WalletInfoExt &w){
                    w.order_blocked = pos_blocked - _position_blocked;
                }, true) || ok;
            }
            _position_blocked = pos_blocked;
        }
        return ok;
    }


    void liquidation();
    std::shared_ptr<OrderInternalData> liquidation_order; 

    
    

};

}
