#pragma once

#include "ifc/defs.hpp"
#include "ifc/market_events.hpp"
#include "ifc/stream.hpp"
#include "ifc/stream_defs.hpp"
#include "instrument_base.hpp"

#include <chrono>
#include <memory>
namespace quarkbot {

class SimulatedOrder;

class SimulatedInstrument : public InstrumentBase{
public:

    SimulatedInstrument(
        const Info &info, 
        PUnderlyingCurrency quote, 
        PUnderlyingCurrency asset, 
        PUnderlyingCurrency pnl, 
        PExchange exchange
    );

    void connect(std::shared_ptr<IDataSource> data_src,  std::string_view stream_topic);

    virtual Info get_info() const override {return _info;}
    virtual PUnderlyingCurrency get_quote_currency() const override {return _quote_currency;}
    virtual PUnderlyingCurrency get_asset() const override {return _asset_currency;}
    virtual PUnderlyingCurrency get_pnl_currency() const override {return _pnl_currency;}
    virtual PExchange get_exchange() const override {return _exchange;}

    void place_order(std::shared_ptr<SimulatedOrder> order);    
    void cancel_order(std::shared_ptr<SimulatedOrder> order);

    
    virtual std::shared_ptr<IEventStreamBase> subscribe_stream_internal(StreamTypeItem::Type type, const StreamParams &params) const override;
    

protected:

    struct OrderRecord {
        std::shared_ptr<SimulatedOrder> order;
        bool stopped;
        Decimal fill_amount;
    };


    Info _info;
    PUnderlyingCurrency _quote_currency;
    PUnderlyingCurrency _asset_currency;
    PUnderlyingCurrency _pnl_currency;
    PExchange _exchange;

    template<StreamType T>
    class MyDataSource: public IDataReceiver {
    public:
        std::weak_ptr<SimulatedInstrument> _owner;
        MyDataSource(std::weak_ptr<SimulatedInstrument> owner):_owner(owner) {}
        virtual MarketStreamTypeItem::Type get_type() const noexcept override {
            return T::type;
        }
        virtual void on_data_received(const MarketStreamTypeItem &data) noexcept override {
            auto ptr = _owner.lock();
            ptr->on_data(static_cast<const T &>(data));
        }
    };

    void on_data(const Quote &x);
    void on_data(const Trade &x);



    std::optional<Quote> _last_quote;
    std::vector<OrderRecord> _orders;
    std::mutex _mx;
    std::shared_ptr<MyDataSource<Quote> > _quote_stream;
    std::shared_ptr<MyDataSource<Trade>  > _trade_stream;

    template<typename Param>
    void run_matching(Param p);

    Fill create_fill(Decimal price, Decimal amount, Side side, std::chrono::system_clock::time_point tm, std::string_view name);

};


}