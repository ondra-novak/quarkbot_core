
#pragma once 

#include "basic_coro/awaitable_transform.hpp"
#include "basic_coro/cancel_signal.hpp"
#include "ifc/stream.hpp"
#include "ifc/stream_defs.hpp"
namespace quarkbot {

template<StreamType T, template<class> class Publisher>
class StreamSubscriber: public IEventStream<T> {
public:

    using Event = typename IEventStream<T>::Event;
    using MyPub = Publisher<T>;

    virtual coro::awaitable<const Event &> read() {
        if (!_publisher) _event.set_eof();        
        if (_event.eof()) return _event;
        return _trn(_publisher->next_event(_event.revisiton, &_signal),[this](bool b) -> const Event &{
            if (b) {
                _publisher->read(_event);
            } else {
                _event.revision = closed_stream;
            }
            return _event;
        });
    }
    virtual bool is_open() const {return _publisher;}
    virtual void close() {
        if (_publisher) {
            _publisher->cancel(&_signal);
            _publisher.reset();
        }
    }

    StreamSubscriber(std::shared_ptr<MyPub> publisher): _publisher(std::move(publisher)) {}

protected:
    Event _event;
    coro::cancel_signal _signal;
    std::shared_ptr<MyPub> _publisher;
    coro::awaitable_transform<coro::awaitable<bool>, StreamSubscriber *> _trn;



};

}