#pragma once


#include "ifc/defs.hpp"
#include "ifc/exchange.hpp"
#include "ifc/market_events.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/reporter.hpp"
#include "ifc/stream_defs.hpp"
#include "ifc/streaming.hpp"
#include "ifc/underlying.hpp"
#include "simaccount.hpp"
#include "simexecutor.hpp"
#include "streaming/lock_free_publisher.hpp"
#include "streaming/publisher_manager.hpp"
#include <memory>
#include <unordered_map>
namespace quarkbot {

class SimInstrument;
class SimTradableInstrument;

class SimExchange final: public IExchange, public std::enable_shared_from_this<SimExchange> {
public:
    
    using QuotePublisher = LockFreePublisher<Quote, 1>;
    using TradePublisher = LockFreePublisher<Trade, 1>;
    using ClosedBarPublisher = LockFreePublisher<ClosedBar, 1>;
    using TradeCounterPublisher = LockFreePublisher<TradeCounter, 1>;

    ///this function creates empty account, credentials are ignored
    virtual PAccount create_account(const std::string &name, const std::string &credentials) const override;
    virtual std::vector<PMarketInstrument> get_market_instruments() const override;
    virtual std::vector<UnderlyingCurrency> get_all_currencies() const override;
    virtual std::string_view get_name() const override;

    std::unique_ptr<IEventStreamBase> subscribe_stream(std::shared_ptr<SimInstrument> instrument,std::shared_ptr<SimAccount> account, StreamTypeItem::Type type, const StreamParams *params);
    PTradableInstrument create_tradable_instrument(std::shared_ptr<SimInstrument> instrument,std::shared_ptr<SimAccount> account);

    PMarketInstrument create_instrument(IMarketInstrument::Info def);
    UnderlyingCurrency create_currency(std::string_view name, bool is_unified = true);
    UnderlyingCurrency create_currency(std::string_view name) const override;
    
    virtual void set_reporter(PReporter reporter) override {_executor.set_reporter(std::move(reporter));}
    void set_slippage(double slippage) { _executor.set_slippage(slippage); }

    ///create account, set up initial wallet
    PAccount create_account(std::string name, std::span<std::pair<std::string, Decimal> > wallet);

    void on_event(const std::string &instrument, Quote qt);
    void on_event(const std::string &instrument, Trade tr);

    bool cancel_all_orders(PTradableInstrument instrument );
    void cancel_order(Order ord);
    void place_order(Order ord);


protected:

    std::unordered_map<std::string, std::weak_ptr<SimInstrument> > _instrument_names;
 
    PublisherManager _streams;
    SimExecutor _executor;
    std::vector<std::weak_ptr<SimTradableInstrument> > _tradable_instruments;

    std::shared_ptr<SimInstrument> resolve_instrument(const std::string &instr);


    template<typename T, typename Pub>
    std::unique_ptr<IEventStreamBase> connect_to(std::shared_ptr<SimInstrument> instrument, const StreamParams *params);


};


}