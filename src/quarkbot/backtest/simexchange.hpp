#pragma once


#include "../common/orderdata.hpp"
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/defs.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/market_instrument.hpp"
#include "quarkbot/stream/auction.hpp"
#include "quarkbot/stream/orderbook.hpp"
#include "quarkbot/types.hpp"
#include "quarkbot/underlying.hpp"
#include "quarkbot/abstract/iexchange.hpp"
#include "../streaming/all_market_stream_manager.hpp"
#include "simaccount.hpp"
#include "simexecutor.hpp"
#include <chrono>
#include <concepts>
#include <memory>
#include <string>
#include <unordered_map>
namespace quarkbot {

class SimInstrument;
class SimTradableInstrument;

class SimExchange final: public IExchange, public std::enable_shared_from_this<SimExchange> {
public:
    
    ///this function creates empty account, credentials are ignored
    virtual PAccount create_account(const std::string &name, const std::string &credentials)  override;
    virtual std::vector<MarketInstrument> get_market_instruments() override;
    virtual std::vector<UnderlyingCurrency> get_all_currencies()  override;
    virtual std::string_view get_name() const override;

    std::shared_ptr<IEventStreamBase> subscribe_stream(std::shared_ptr<SimInstrument> instrument, std::size_t type, const void *params);
    PTradableInstrument create_tradable_instrument(std::shared_ptr<SimInstrument> instrument,std::shared_ptr<SimAccount> account);

    ///add simulated instrument to exchange, returns a handle to it. If the same name already exists, returns the existing one (and updates its definition)
    /** This function is used to add a new simulated instrument to the exchange by a backtest controller
        (don't be confused by function create_instrument which is intended for strategy)
        It creates simulated instrument and links it to the exchange,
        The info structure can contain unbound underlying currencies (will be bound to the exchange by this function),    
    */
    template<std::derived_from<IMarketInstrument::Info> _Info>
    PMarketInstrument add_instrument(const _Info &def) {
        std::unique_ptr<IMarketInstrument::Info> pinfo = std::make_unique<_Info>(def);
        return add_instrument(std::move(pinfo));
    }

    PMarketInstrument add_instrument(std::unique_ptr<IMarketInstrument::Info> def);
    
    
    virtual PMarketInstrument create_instrument(std::string_view id, InstrumentType type) override;
    UnderlyingCurrency create_currency(std::string_view name, bool is_unified = true);
    UnderlyingCurrency create_currency(std::string_view name) const;
    

    void set_slippage(double slippage) { _executor.set_slippage(slippage); }
    void set_latency(std::chrono::system_clock::duration dur) {_executor.set_latency(dur);}
    void set_reporter(ReportSink sink) {_executor.set_report_sink(std::move(sink));}
    void set_history_source(BacktestHistorySource source) {_hist_source = std::move(source);}
    const BacktestHistorySource get_history_source() const {return _hist_source;}

    ///create account, set up initial wallet
    PAccount create_account(std::string name, std::span<const std::pair<std::string, Decimal> > wallet);

    void on_event(const std::string &instrument, Quote qt);
    void on_event(const std::string &instrument, Trade tr);
    void on_event(const std::string &instrument, Auction au);
    void on_event(const std::string &instrument, const OrderBookSnapshot &sn);
    void on_event(const std::string &instrument, const OrderBookIncrement &inc);

    bool cancel_all_orders(PTradableInstrument instrument );
    void cancel_order(POrder ord);
    void cancel_order(IOrder *ord);
    void place_order(POrder ord);

    void stop_on(std::stop_token tkn);


protected:

    struct InstrumentRef {
        std::weak_ptr<SimInstrument> _ref;
        std::unique_ptr<IMarketInstrument::Info> info;
        std::shared_ptr<SimInstrument> get(const std::shared_ptr<SimExchange> &owner);
    };



    std::unordered_map<std::string, InstrumentRef> _instrument_names;
    AllMarketStreamManager<std::string> _streams;
    
    SimExecutor _executor;
    std::vector<std::weak_ptr<SimTradableInstrument> > _tradable_instruments;

    std::shared_ptr<SimInstrument> resolve_instrument(const std::string &instr);

    ///Find instrument definition by name without instantiating SimInstrument
    /** @return pointer to the record, nullptr when the name is unknown to this exchange */
    InstrumentRef *find_instrument_ref(const std::string &instr);

    ///Get already existing SimInstrument for given name, never creates a new one
    std::shared_ptr<SimInstrument> instrument_for_event(const std::string &instr);

    BacktestHistorySource _hist_source;




};


}