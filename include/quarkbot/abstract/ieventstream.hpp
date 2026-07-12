#pragma once

#include "../defs.hpp"
#include "quarkbot/abstract/default_shared.hpp"
#include <memory>
#include <stop_token>
namespace quarkbot {

template<typename T> concept HasStreamView = requires(T val) {
    {val.view()};
};


template<typename T>
struct StreamViewType {
    using type = T;
};
template<HasStreamView T>
struct StreamViewType<T> {
    using type = std::remove_reference_t<decltype(std::declval<T>().view())>;
};


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
    virtual awaitable<bool> receive(ViewType &ref) = 0;

    ///read next event, if available, and copy it to ref, also report count of missed events
    /**
    @param ref reference to object where event data will be copied.
    @param missed reference to variable where count of missed events will be stored
    @retval true new event is available and copied to ref
    @retval false stream is closed or no new event is available
    */
    virtual coro::awaitable<bool> receive(ViewType &ref, std::size_t &missed) = 0;

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

    class Closed;
    class Silent;

protected:
        struct StopCBInvokable {
        IEventStream *ptr;
        void operator()() {
            ptr->close();
        }
    };
    using StopCB = std::stop_callback<StopCBInvokable>;

    std::optional<StopCB> _stop_callback;

    

};

///declares stream which is always closed on creating
/**
To get this stream, use static function get_instance(). Single instance is shared accros process
because it has no state, it is closed for good
*/
template<typename ViewType>
class IEventStream<ViewType>::Closed: public IEventStream<ViewType> {
public:
    virtual bool is_open() const override {return false;}
    virtual void close()  override {}
    virtual bool current(ViewType &) override  {return false;}
    virtual coro::awaitable<bool> receive(ViewType &, std::size_t &) override  {return false;};
    virtual coro::awaitable<bool> receive(ViewType &) override {return false;};

    static std::shared_ptr<IEventStream<ViewType> > get_instance();

};

template<typename ViewType>
constexpr IEventStream<ViewType>::Closed closed_eventstream;

template<typename ViewType>
inline std::shared_ptr<IEventStream<ViewType> > IEventStream<ViewType>::Closed::get_instance() {
    return default_shared<IEventStream<ViewType> >(closed_eventstream<ViewType>);
}

///declares stream which is never produces any event. But can be closed
/**
you need create instance for this stream. The await operation blocks until the stream is closed. It doesn't
produce any event
*/
template<typename ViewType>
class IEventStream<ViewType>::Silent: public IEventStream<ViewType> {
public:
    virtual bool is_open() const override {return !_closed;}
    virtual void close()  override {
        _closed = true;
        _awaiting(false);
    }
    virtual bool current(ViewType &) override  {return false;}
    virtual coro::awaitable<bool> receive(ViewType &, std::size_t &) override  {
        return [this](auto promise) {
            _awaiting = std::move(promise);
        };
    };
    virtual coro::awaitable<bool> receive(ViewType &) override {
        return [this](auto promise) {
            _awaiting = std::move(promise);
        };
    }

    static std::shared_ptr<IEventStream<ViewType> > create_instance() {
        return std::make_shared<Silent>();
    }
protected:
    bool _closed = false;
    awaitable<bool>::result _awaiting;
};


}