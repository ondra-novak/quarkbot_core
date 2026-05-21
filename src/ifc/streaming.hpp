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

template<typename T> concept HasStreamView = requires(T val) {
    {val.view()};
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
        std::size_t dummy;
        return read_internal(ref, dummy);
    }
    ///read next event, if available, and copy it to ref, also report count of missed events
    /**
    @param ref reference to object where event data will be copied.
    @param missed reference to variable where count of missed events will be stored
    @retval true new event is available and copied to ref
    @retval false stream is closed or no new event is available
    */
    coro::awaitable<bool> next(ViewType &ref, std::size_t &missed) {
        return read_internal(ref, missed);
    }    

    ///read current value regardless on whether it was updated or not
    /**
        use to read last value
        @retval true value ready
        @retval false value can't be retrieved. 
        @note not all streams supports this function. The return value can reflect this feature
     */
    virtual bool current(ViewType &ref) = 0;


    class Null;

protected:
    virtual coro::awaitable<bool> read_internal(ViewType &ref, std::size_t &missed)  = 0;

};

template<typename ViewType>
class IEventStream<ViewType>::Null: public IEventStream<ViewType> {
    virtual bool is_open() const {return false;}
    virtual void close() {}
    virtual bool current(ViewType &) {return false;}
    virtual coro::awaitable<bool> read_internal(ViewType &, std::size_t &) {return false;};
};

template<typename T>
struct StreamViewType {
    using type = T;
};
template<HasStreamView T>
struct StreamViewType<T> {
    using type = decltype(std::declval<T>().view());
};


///Event stream for specific type of events - wrapper over IEventStreamBase, which provides typed access to event data
/**
@tparam T type of events, must satisfy StreamType concept. The stream provides access to event data through T::view() method
*/
template<typename T>
class EventStream {
public:
    ///Type of view returned by T::view() method
    using ViewType = typename StreamViewType<T>::type;

    ///constructor from IEventStreamBase pointer, stream is open if pointer is not null
    EventStream(std::unique_ptr<IEventStream<ViewType> > stream):_stream(std::move(stream)) {}  

    static EventStream from_base(std::unique_ptr<IEventStreamBase> base) {
        return EventStream(std::unique_ptr<IEventStream<ViewType> >(static_cast<IEventStream<ViewType> *>(base.release())));        
    }
    static EventStream create_null() {
        return EventStream(std::make_unique<typename IEventStream<ViewType>::Null>());
    }

    ///check if stream is open
    bool is_open() const {return _stream->is_open();}
    ///close the stream
    void close() {_stream->close();}
    ///conversion to bool - true if stream is open, false if closed
    operator bool() const {return is_open();}
    ///read next event, if available, and copy it to ref
    coro::awaitable<bool> next(T &val) {
        if constexpr (HasStreamView<T>) {
            return _stream->next(val.view());
        } else {
            return _stream->next(val);
        }
    }
        
    ///read next event, if available, and copy it to ref, also report count of missed events     
    coro::awaitable<bool> next(T &val, std::size_t &missed) {return _stream->next(val.view(), missed);}

    bool current(T &val) {
          if constexpr (HasStreamView<T>) {
            return _stream->current(val.view());
        } else {
            return _stream->current(val);
        }
    }
   
    auto get() const {return _stream.get();}

protected:
    std::unique_ptr<IEventStream<ViewType> > _stream;

};


template<typename StreamTypeClass = StreamTypeItem>
class IPublisher {
protected:

    virtual std::unique_ptr<IEventStreamBase> subscribe_stream_internal(std::string_view type, const StreamParams *params) = 0;

public:

    virtual ~IPublisher() = default;

    template<StreamType<StreamTypeClass> T>
    EventStream<T> subscribe() {
        auto x =  subscribe_stream_internal(T::type, stream_params<T>);
        if (x) return EventStream<T>::from_base(std::move(x));
        else return EventStream<T>::create_null();
    }
};


}