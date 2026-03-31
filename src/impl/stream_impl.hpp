#pragma once
#include <basic_coro/awaitable.hpp>
#include "ifc/stream.hpp"
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include <basic_coro/awaitable_transform.hpp>
#include "utils/pubsub.hpp"
#include <chrono>
#include <memory>
#include <mutex>



namespace quarkbot {

template<typename T>
struct EventWithCounter {
    T value;
    std::size_t counter;
};

template<StreamType T, std::size_t limit>
class StreamServer: public Publisher<EventWithCounter<T>, limit> {
public:

    using Super = Publisher<EventWithCounter<T>, limit>;

    void post(const T &value) noexcept {
        std::unique_lock lk(this->_mx);
        this->_queue.push_front({value, _tick_counter++});
        this->on_post(lk);
    }
    void post(T &&value) noexcept {
        std::unique_lock lk(this->_mx);
        this->_queue.push_front({std::move(value), _tick_counter++});
        this->on_post(lk);
    }

protected:
    std::size_t _tick_counter = 0;


};

template<StreamType T, std::size_t limit>
class StreamClient: public Subscriber<EventWithCounter<T>, limit>, public IEventStream<T> {
public:

    using Event = typename IEventStream<T>::Event;
    using Super = Subscriber<EventWithCounter<T>, limit>;

    StreamClient(std::shared_ptr<StreamServer<T,limit> > server)
        :_server(std::move(server)) {
            Super::subscribe(*_server);
        }


    virtual void close() override {
        Super::close();
    }
    virtual bool is_open() const override {
        return !Super::is_closed();
    }
    virtual coro::awaitable<Event> read() override {      
        return _transform(Super::read(), [this](EventWithCounter<T> &&v){
                return Event {
                    std::chrono::system_clock::now(),
                    update_counter(v.counter),
                    std::move(v.value)
                };
        });
    }



protected:


    std::shared_ptr<StreamServer<T, limit> > _server;
    std::size_t _tick_counter = 0;    
    coro::awaitable_transform_r<ProxyResult , awaitable<EventWithCounter<T> >, StreamClient *> _transform;

    std::size_t update_counter(std::size_t counter) {
        auto diff = counter - _tick_counter;
        _tick_counter = counter;
        return diff-1;
    }
};



}