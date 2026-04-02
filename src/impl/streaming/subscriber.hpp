
#pragma once 

#include "basic_coro/awaitable_transform.hpp"
#include "basic_coro/cancel_signal.hpp"
#include "ifc/streaming.hpp"
#include "ifc/stream_defs.hpp"
#include "impl/streaming/publisher_base.hpp"
namespace quarkbot {

template<typename ViewType, typename Publisher>
class StreamSubscriber: public IEventStream<ViewType> {
public:
    using Seq = PublisherBase::Seq;

    virtual bool is_open() const override {
        return !_closed;
    }
    virtual void close() override {
        _closed = true;
        _publisher->cancel(&sig);
    }
    virtual coro::awaitable<bool> read_internal(ViewType &ref, std::size_t *missed) override {
        if (_closed) return false;
        return _trn(_publisher->next(_seq, &sig),[this, missed, &ref](bool b){
            if (b) {
                Seq cur = _seq;
                _publisher->read(ref, _seq);
                if (missed) *missed = _seq - cur - 1;                
            } 
            return b;
        });
    }

    StreamSubscriber(std::shared_ptr<Publisher> publisher):_publisher(std::move(publisher)) {}
        
protected:

    Seq _seq = 0; //current revision, used for polling
    std::shared_ptr<Publisher> _publisher; //reference to publisher, used for polling
    bool _closed = false; //flag indicating whether stream is closed
    coro::awaitable_transform<coro::awaitable<bool>, StreamSubscriber *, ViewType &, std::size_t * > _trn;
    coro::cancel_signal sig;

};

}