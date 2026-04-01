#pragma once

#include "basic_coro/awaitable_transform.hpp"
#include "basic_coro/cancel_signal.hpp"
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include "ifc/stream.hpp"
namespace quarkbot {

class PublisherBase {
public:
        using Factory = std::shared_ptr<IEventStreamBase> (*)(std::shared_ptr<PublisherBase>);

        PublisherBase(Factory factory):_factory(factory) {}

        using Revision = StreamEventRevision;
        
        ///Consumer definition - registration of awaiting coroutine
        struct Consumer {
            ResultAndExecWorker<bool> _result; //result of suspended coroutine associated with its execution worker
            coro::cancel_signal *_cancel; // if not null, awaiting can be canceled
        };

                ///cancel awaiting operation
        /**
        @param sig pointer to associated cancel signal, which serves as identification and also signal distributor. Must not be
        null, other function fails with false
        @retval true awaiter was removed and coroutine was scheduled with eof indication as result
        @retval false awaiter was not found, it is possible that the coroutine is running and processing some data. The
        canel_signal prevents to next co_await on this object, but caller must use a proper synchronization to 
        join coroutine's execution with its own thread.

        @note MT safe

        */
        bool cancel(coro::cancel_signal *sig) {            
            if (!sig || sig->is_canceled()) return false; //ignore if already canceled
            std::lock_guard _(_mx);
            //try to find awaiter
            auto found =  std::find_if(_awaiters.begin(), _awaiters.end(), [&](const Consumer &c){
                return c._cancel == sig;
            });
            //request cancel on signal
            sig->request_cancel();
            //if found, remove it and report true
            if (found != _awaiters.end()) {
                found->_result(false);  //close the stream for this consumer only
                _awaiters.erase(found); 
                return true;
            }
            //otherwise report false
            return false;
        }

        
        auto create_subscriber(std::shared_ptr<PublisherBase> this_shared)const {
            return _factory(this_shared);
        }

protected:

        mutable std::mutex _mx;
        std::vector<Consumer> _awaiters;

        void flush_consumers(bool st) {
            for (auto &x: _awaiters) x._result(st);
            _awaiters.clear();
        }
        Factory _factory;

};

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