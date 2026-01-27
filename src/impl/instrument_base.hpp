#pragma once

#include "ifc/defs.hpp"
#include "ifc/market_events.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/stream.hpp"
#include "stream_impl.hpp"

#include <memory>
namespace quarkbot {

class InstrumentBase : public IMarketInstrument , public std::enable_shared_from_this<InstrumentBase>{
public:


    ///connect instrument to data stream
    /**
    must be called after creation to allow forward market events. 
     */
    void connect(std::shared_ptr<IDataSource> data_src,  std::string_view stream_topic) {
        _data_src = std::move(data_src);
        _stream_topic = stream_topic;
    }        


    template<StreamType T>
    class MyServer: public StreamServer<T>, public IDataReceiver {
    public:
        virtual StreamTypeItem::Type get_type() const noexcept override {
            return T::type;
        }
        virtual void on_data_received(const StreamTypeItem &data) noexcept override {
            this->post(static_cast<const T &>(data));
        }
    };

    template<>
    class MyServer<TradeCounter> : public StreamServer<TradeCounter>, public IDataReceiver {
    public:        
        virtual StreamTypeItem::Type get_type() const noexcept override {
            return Trade::type;
        }

        virtual void on_data_received(const StreamTypeItem &data) noexcept override {
            const Trade &t = static_cast<const Trade &>(data);
            accum.trades += 1;
            accum.last_price = t.price;
            accum.volume += t.size;
            accum.time = t.time;
            this->post(accum);
        }

    protected:
        TradeCounter accum;
    };
    
    virtual std::shared_ptr<IMarketEventStreamBase> subscribe_stream_internal(StreamTypeItem::Type type) const {
        if (type == Quote::type) return this->subscribe<Quote>(_quote_server);
        if (type == Trade::type) return this->subscribe<Trade>(_trade_server);
        if (type == OrderBook::type) return this->subscribe<OrderBook>(_orderbook_server);
        if (type == TradeCounter::type) return this->subscribe<TradeCounter>(_trade_counter_server);
        return {};
    }


protected:
  std::shared_ptr<IDataSource> _data_src;
  std::string _stream_topic;
  mutable std::atomic<std::weak_ptr<MyServer<Quote> > > _quote_server;
  mutable std::atomic<std::weak_ptr<MyServer<Trade> > > _trade_server;
  mutable std::atomic<std::weak_ptr<MyServer<OrderBook> > >_orderbook_server;
  mutable std::atomic<std::weak_ptr<MyServer<TradeCounter> > >_trade_counter_server;

  template<StreamType T, typename ServerRef, typename ServerType = ServerRef>
  std::shared_ptr<IMarketEventStreamBase> subscribe(std::atomic<std::weak_ptr<ServerRef> > &wkref) const {    
    std::weak_ptr<ServerRef> wk = wkref.load();    
    std::shared_ptr<ServerRef> server = wk.lock();
    while (!server) {        
        server = std::make_shared<ServerType>();
        if (!_data_src->subscribe(_stream_topic, server)) return {};
        std::weak_ptr<ServerRef > new_wk(server);
        if (!wkref.compare_exchange_strong(wk, new_wk)) server = wk.lock();        
    }
    return std::make_shared<StreamClient<T> >(std::move(server));
  }
  
};

} // namespace quarkbot