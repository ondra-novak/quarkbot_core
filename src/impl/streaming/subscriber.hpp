
#pragma once 

#include "basic_coro/awaitable_transform.hpp"
#include "basic_coro/cancel_signal.hpp"
#include "ifc/stream_defs.hpp"
#include "impl/streaming/publisher_base.hpp"
namespace quarkbot {

template<typename ViewType, typename Publisher>
class StreamSubscriber: public IEventStream<ViewType> {
public:
    using Seq = PublisherBase::Seq;

    virtual bool is_open() const override {
        return !_sig.is_canceled();
    }
    virtual void close() override {
        _publisher->cancel(&_sig);
    }


    virtual coro::awaitable<bool> receive(ViewType &ref) {
        if (_sig.is_canceled()) return false;
        if (_publisher->read(ref, _seq)) return true;
        return AutoReceivePromise(this, ref);
    }

    virtual coro::awaitable<bool> receive(ViewType &ref, std::size_t &missed) override {
        if (_sig.is_canceled()) return false;
        auto seqref = _seq;
        if (_publisher->read(ref, _seq)) {
            missed = _seq - seqref -1;
            return true;
        }
        return AutoReceivePromiseMissed(this, ref, missed);
    }

     virtual bool current(ViewType &ref) override {
        Seq cur = _seq;
        if (!_publisher->read(ref, cur)) {
            if (_seq==0) return false;
            --cur;
            return _publisher->read(ref,cur);
        }
        _seq = cur;
        return true;        
     }

    StreamSubscriber(std::shared_ptr<Publisher> publisher):_publisher(std::move(publisher)) {}
        
protected:

    Seq _seq = 0; //current revision, used for polling
    std::shared_ptr<Publisher> _publisher; //reference to publisher, used for polling
    coro::awaitable_transform<coro::awaitable<bool>, StreamSubscriber *, ViewType &, std::size_t * > _trn;
    coro::cancel_signal _sig;

    //struct uses fact, that before awaitable result is set, the destructor of this instance is called
    //because this happens when stream has value or closed, we can use this to fetch actual value from the stream
    struct AutoReceivePromise {
        StreamSubscriber *me;
        ViewType &ref;        
        AutoReceivePromise(StreamSubscriber *me, ViewType &ref):me(me),ref(ref) {}        
        AutoReceivePromise(AutoReceivePromise &&other):me(other.me), ref(other.ref) {other.me = nullptr;}
        ~AutoReceivePromise() {
            if (me && me->is_open()) {
                me->_publisher->read(ref, me->_seq);
            }
        }
        void operator()(awaitable<bool>::result result) {
            me->_publisher->next(me->_seq, std::move(result), &me->_sig);
        }
    };

    //struct uses fact, that before awaitable result is set, the destructor of this instance is called
    //because this happens when stream has value or closed, we can use this to fetch actual value from the stream
    struct AutoReceivePromiseMissed {
        StreamSubscriber *me;
        ViewType &ref;        
        std::size_t &missed;
        AutoReceivePromiseMissed(StreamSubscriber *me, ViewType &ref,std::size_t &missed):me(me),ref(ref),missed(missed) {
            missed = static_cast<std::size_t>(me->_seq);
        }        
        ~AutoReceivePromiseMissed() {
            if (!me->is_open()) {                
                me->_publisher->read(ref, me->_seq);
                missed = static_cast<std::size_t>(me->_seq) - missed - 1;
            } else {
                missed = 0;
            }
        }
        void operator()(awaitable<bool>::result result) {
            me->_publisher->next(me->_seq, std::move(result), &me->_sig);
        }
    };

};


}