#pragma once

#include "../defs.hpp"
#include <stop_token>
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
    awaitable<bool> receive(ViewType &ref) {
        static thread_local std::size_t dummy;   
        return read_internal(ref, dummy);
    }
    ///read next event, if available, and copy it to ref, also report count of missed events
    /**
    @param ref reference to object where event data will be copied.
    @param missed reference to variable where count of missed events will be stored
    @retval true new event is available and copied to ref
    @retval false stream is closed or no new event is available
    */
    coro::awaitable<bool> receive(ViewType &ref, std::size_t &missed) {
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


    void set_stop_token(std::stop_token token) {
        _stop_callback.emplace(std::move(token), StopCBInvokable{this});
    }
    void set_stop_token(const std::stop_source &source) {
        set_stop_token(source.get_token());
    }

    class Null;

protected:
        struct StopCBInvokable {
        IEventStream *ptr;
        void operator()() {
            ptr->close();
        }
    };
    using StopCB = std::stop_callback<StopCBInvokable>;

    std::optional<StopCB> _stop_callback;


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
    using type = std::remove_reference_t<decltype(std::declval<T>().view())>;
};



}