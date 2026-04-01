#pragma once

#include "basic_coro/cancel_signal.hpp"
#include "ifc/stream.hpp"
#include "ifc/stream_defs.hpp"
#include <basic_coro/awaitable_transform.hpp>
namespace quarkbot {

template<StreamType T, template<class> class Publisher>
class StreamSubscriber: public IEventStream<T> {
public:

    using Event = typename IEventStream<T>::Event;
    using MyPub = Publisher<T>;

    virtual coro::awaitable<const Event &> read() {
        return _trn(_publisher->next_event(_event.revisiton, &signal),[this](bool b) -> const Event &{
            if (b) {
                _publisher->read(_event);
            } else {
                _event.revision = closed_stream;
            }
            return _event;
        });
    }
    virtual bool is_open() const {return true;}
    virtual void close();


protected:
    Event _event;
    coro::cancel_signal signal;
    std::shared_ptr<MyPub> _publisher;
    coro::awaitable_transform<coro::awaitable<bool>, StreamSubscriber *> _trn;



};

}