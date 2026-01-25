#pragma once

#include "ifc/defs.hpp"
#include "ifc/market_events.hpp"
#include "ifc/market_instrument.hpp"
#include "ifc/stream.hpp"
#include "impl/stream_impl.hpp"
#include <memory>
namespace quarkbot {

class InstrumentBase : public IMarketInstrument , public std::enable_shared_from_this<InstrumentBase>{
public:
    explicit InstrumentBase(Info info, 
                            PUnderlyingCurrency currency, 
                            std::shared_ptr<IDataSource> data_src, 
                            std::string_view stream_topic)
      : _info(std::move(info))
      , _currency(currency)
      , _data_src(std::move(data_src))
      ,_stream_topic(stream_topic) {}


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
    
    virtual std::shared_ptr<IMarketEventStreamBase> subscribe_stream_internal(StreamTypeItem::Type type) const {
        if (type == Quote::type) return this->subscribe(_quote_server);
        if (type == Trade::type) return this->subscribe(_trade_server);
        if (type == OrderBook::type) return this->subscribe(_orderbook_server);
        if (type == OrderBookEntry::type) return this->subscribe(_orderbook_increment_server);
        return {};
    }


protected:
  Info _info;
  PUnderlyingCurrency _currency;
  std::shared_ptr<IDataSource> _data_src;
  std::string _stream_topic;
  mutable std::atomic<std::weak_ptr<MyServer<Quote> > > _quote_server;
  mutable std::atomic<std::weak_ptr<MyServer<Trade> > > _trade_server;
  mutable std::atomic<std::weak_ptr<MyServer<OrderBook> > >_orderbook_server;
  mutable std::atomic<std::weak_ptr<MyServer<OrderBookEntry> > >_orderbook_increment_server;

  template<StreamType T>
  std::shared_ptr<IMarketEventStreamBase> subscribe(std::atomic<std::weak_ptr<MyServer<T> > > &wkref) const {    
    std::weak_ptr<MyServer<T> > wk = wkref.load();    
    std::shared_ptr<MyServer<T> > server = wk.lock();
    if (!server) {        
        server = std::make_shared<MyServer<T> >();
        if (!_data_src->subscribe(_stream_topic, server)) return {};
        std::weak_ptr<MyServer<T> > new_wk(server);
        if (!wkref.compare_exchange_strong(wk, new_wk)) return subscribe(wkref);
    }
    return std::make_shared<StreamClient<T> >(std::move(server));
  }
  
};

} // namespace quarkbot