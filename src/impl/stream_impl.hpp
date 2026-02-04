#pragma once
#include "coro/src/basic_coro/awaitable.hpp"
#include "coro/src/basic_coro/awaiting_callback.hpp"
#include "ifc/stream.hpp"
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include "utils/pubsub.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>



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


    virtual void close() {
        Super::close();
    }
    virtual coro::awaitable<Event> read() {        
        auto awt = Super::read();
        if (awt.await_ready()) {
            EventWithCounter<T> &&v = awt.await_resume();
            return coro::awaitable<Event>({
                std::chrono::system_clock::now(),
                update_counter(v.counter),
                std::move(v.value)
            });
        } else {
            return [this](auto prom) {                
                _result = std::move(prom);
                _cb.set_awaiter(Super::read());
                _cb.set_callback([this](coro::awaitable<EventWithCounter<T> > &awt){
                    if (awt.has_value()){
                        EventWithCounter<T> &&v = awt.await_resume();
                        _result(Event {
                                std::chrono::system_clock::now(),
                                update_counter(v.counter),
                                std::move(v.value)
                            });
                        } else _result(std::nullopt);
                });
                return _cb.await();                
            };
        };
    }

protected:


    std::shared_ptr<StreamServer<T, limit> > _server;
    std::size_t _tick_counter = 0;    
    coro::awaiting_callback<coro::awaitable<EventWithCounter<T> >, StreamClient *> _cb;
    IExecutionWorker::proxy_result<Event> _result;

    std::size_t update_counter(std::size_t counter) {
        auto diff = counter - _tick_counter;
        _tick_counter = counter;
        return diff-1;
    }
};



}