#pragma once


#include "ifc/abstract/orderdata.hpp"
#include "ifc/defs.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/types.hpp"
#include "ifc/underlying.hpp"
#include "ifc/abstract/iexchange.hpp"
#include "impl/streaming/all_market_stream_manager.hpp"
#include "simaccount.hpp"
#include "simexecutor.hpp"
#include <memory>
#include <unordered_map>
namespace quarkbot {

class SimInstrument;
class SimTradableInstrument;

class SimExchange final: public IExchange, public std::enable_shared_from_this<SimExchange> {
public:
    
    ///this function creates empty account, credentials are ignored
    virtual Account create_account(const std::string &name, const std::string &credentials)  override;
    virtual std::vector<MarketInstrument> get_market_instruments() override;
    virtual std::vector<UnderlyingCurrency> get_all_currencies()  override;
    virtual std::string_view get_name() const override;

    std::unique_ptr<IEventStreamBase> subscribe_stream(std::shared_ptr<SimInstrument> instrument, std::size_t type, const void *params);
    PTradableInstrument create_tradable_instrument(std::shared_ptr<SimInstrument> instrument,std::shared_ptr<SimAccount> account);

    ///add simulated instrument to exchange, returns a handle to it. If the same name already exists, returns the existing one (and updates its definition)
    /** This function is used to add a new simulated instrument to the exchange by a backtest controller
        (don't be confused by function create_instrument which is intended for strategy)
        It creates simulated instrument and links it to the exchange,
        The info structure can contain unbound underlying currencies (will be bound to the exchange by this function),    
    */
    PMarketInstrument add_instrument(const IMarketInstrument::Info def);
    
    
    virtual MarketInstrument create_instrument(std::string_view id, InstrumentType type) override;
    UnderlyingCurrency create_currency(std::string_view name, bool is_unified = true);
    UnderlyingCurrency create_currency(std::string_view name) const;
    
    void set_slippage(double slippage) { _executor.set_slippage(slippage); }

    ///create account, set up initial wallet
    PAccount create_account(std::string name, std::span<std::pair<std::string, Decimal> > wallet);

    void on_event(const std::string &instrument, Quote qt);
    void on_event(const std::string &instrument, Trade tr);

    bool cancel_all_orders(PTradableInstrument instrument );
    void cancel_order(POrderAData ord);
    void cancel_order(OrderInternalData *ord);
    void place_order(POrderAData ord);


protected:

    struct InstrumentRef {
        std::weak_ptr<SimInstrument> _ref;
        IMarketInstrument::Info info;
        std::shared_ptr<SimInstrument> get(const std::shared_ptr<SimExchange> &owner);
    };

    std::unordered_map<std::string, InstrumentRef> _instrument_names;
    AllMarketStreamManager<std::string> _streams;
    
    
    SimExecutor _executor;
    std::vector<std::weak_ptr<SimTradableInstrument> > _tradable_instruments;

    std::shared_ptr<SimInstrument> resolve_instrument(const std::string &instr);




};


}