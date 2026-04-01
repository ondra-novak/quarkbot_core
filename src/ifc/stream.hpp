#pragma once

#include <basic_coro/awaitable.hpp>
#include "defs.hpp"
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>

namespace quarkbot {

class IEventStreamBase {
public:
    virtual ~IEventStreamBase() = default;
    ///Returns true, if stream is open, false if closed
    /**
    @note For unsupported stream, you always receives closed stream. Otherwise you need to receive nullopt to set this state false
     */
    virtual bool is_open() const = 0;
    ///Close the stream
    /**
       If there is pending read, it is immediately finished with nullopt
    */
    virtual void close();
};

///Market event stream
/**
@tparam T type of event data
 */
template<StreamType T>
class IEventStream : public IEventStreamBase{
public:
    virtual ~IEventStream();

    ///Event structure
    struct Event {
        ///time of receiving
        std::chrono::system_clock::time_point received;
        ///number of missed events if processing was too slow
        std::size_t missed;
        ///event data
        T data;
    };

    ///Read next event
    /**
    @return awaitable returning next event (asynchronous). Returns std::nullopt on eof (awaitable acts as optional).
    If there are missed events, it immediately returns last received event with missed count set appropriately
    There can be only one pending read at a time. If you need multiple concurrent reads, create multiple streams.
     */
    virtual coro::awaitable<Event> read() = 0;    
    class Null;

};

template<StreamType T>
class IEventStream<T>::Null: public IEventStream<T> {
public:
    virtual coro::awaitable<Event> read() override {
        return [](auto prom) {
            return prom(std::make_exception_ptr(std::runtime_error("Unsupported stream")));
        };
    }
    virtual bool is_open() const override {return false;};
    virtual void close() override {}
};




///Base class for generic data source receiver
/**
Data sources are used internally to listen various streams containing multiple instruments and events.
To listen market data streams, you need to retrieve IMarketEventStream from the instrument.
*/
class IDataReceiver {
public:
    virtual ~IDataReceiver() = default;
    ///Retrieve type of stream for this instance
    virtual MarketStreamTypeItem::Type get_type() const noexcept = 0;
    ///called when data arrived
    /**
    @note data are probably broadcasted synchronously and the function is called in context of stream's thread
    */    
    virtual void on_data_received(const MarketStreamTypeItem &data) noexcept = 0;

};

///Allows to subscribe to a stream
class IDataSource {
public:
    virtual ~IDataSource() = default;
    ///Subscribes a receiver
    /**
    @param topic in most cases, topic is also instrument id, but depends on an exchange. There can be multiple stream types under
    single topic
    @param receiver shared pointer to receiver. 

    @retval true subscribed
    @retval false stream is not available, unknown type or unsupported receiver type (for example, asking for orderbook snapshot when
    L2 data are not available)

    @note to keep subscription, you need to hold reference to the receive. One the reference is dropped, received is immediately removed
    from the subscribtion
     */
    virtual bool subscribe(std::string_view topic, std::shared_ptr<IDataReceiver> receiver) = 0;
};

}

