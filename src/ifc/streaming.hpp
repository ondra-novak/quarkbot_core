#pragma once

#include "basic_coro/awaitable.hpp"
#include "ifc/stream_defs.hpp"
#include <memory>
namespace quarkbot {


///Base class for event stream
class IEventStreamBase {
public:
    virtual ~IEventStreamBase() = default;
    ///check if stream is open
    /**
    @retval true stream is open
    @retval false stream is closed
     */
    virtual bool is_open() const = 0;
    ///close the stream
    /**
    Function can be called from any thread, causes that awaiting coroutine is resolved with false.
    Stream cannot be reopened.
     */
    virtual void close() = 0;
};

template<typename ViewType>
class IEventStream: public IEventStreamBase {
public:

    ///read next event, if available, and copy it to ref
    /**
    @param ref reference to object where event data will be copied. 
    @retval true new event is available and copied to ref
    @retval false stream is closed or no new event is available
    */
    coro::awaitable<bool> next(ViewType &ref) {
        return read_internal(ref, nullptr);
    }
    ///read next event, if available, and copy it to ref, also report count of missed events
    /**
    @param ref reference to object where event data will be copied.
    @param missed reference to variable where count of missed events will be stored
    @retval true new event is available and copied to ref
    @retval false stream is closed or no new event is available
    */
    coro::awaitable<bool> next(ViewType &ref, std::size_t &missed) {
        return read_internal(ref, &missed);
    }    

    class Null;

protected:
    virtual coro::awaitable<bool> read_internal(ViewType &ref, std::size_t *missed)  = 0;

};

template<typename ViewType>
class IEventStream<ViewType>::Null: public IEventStream<ViewType> {
    virtual bool is_open() const {return false;}
    virtual void close() {}
    virtual coro::awaitable<bool> read_internal(ViewType &, std::size_t *) {return false;};
};


///Event stream for specific type of events - wrapper over IEventStreamBase, which provides typed access to event data
/**
@tparam T type of events, must satisfy StreamType concept. The stream provides access to event data through T::view() method
*/
template<StreamType T>
class EventStream {
public:
    ///Type of view returned by T::view() method
    using ViewType = decltype(std::declval<T>().view());

    ///default constructor creates closed stream
    EventStream():_stream(std::make_unique<typename IEventStream<ViewType>::Null>()) {}
    ///constructor from IEventStreamBase pointer, stream is open if pointer is not null
    EventStream(std::unique_ptr<IEventStream<ViewType> > stream):_stream(std::move(stream)) {}  

    EventStream(std::unique_ptr<IEventStreamBase> stream)
        :_stream(static_cast<IEventStream<ViewType> *>(stream.release())) {}

    ///check if stream is open
    bool is_open() const {return _stream->is_open();}
    ///close the stream
    void close() {_stream->close();}
    ///conversion to bool - true if stream is open, false if closed
    operator bool() const {return is_open();}
    ///read next event, if available, and copy it to ref
    coro::awaitable<bool> next(T &val) {return _stream->next(val.view());}
    ///read next event, if available, and copy it to ref, also report count of missed events     
    coro::awaitable<bool> next(T &val, std::size_t &missed) {return _stream->next(val.view(), missed);}
   

protected:
    std::unique_ptr<IEventStream<ViewType> > _stream;

};



}