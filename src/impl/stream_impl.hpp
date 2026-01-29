#pragma once
#include "ifc/stream.hpp"
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <vector>



namespace quarkbot {

template<StreamType T>
class StreamClient;
template<StreamType T>
class StreamServer;


template<MarketStreamType T>
class StreamServer<T> {
public:

    using Event = typename IMarketEventStream<T>::Event;

    bool await(std::size_t counter, std::shared_ptr<StreamClient<T> > client);
    Event get_last_event(std::size_t &my_counter) const;
    bool event_ready(std::size_t counter) const;
    void post(const T &value) noexcept;
    void post(T &&value) noexcept;
    
    template<typename Fn>
    auto sync(Fn &&fn) {
        std::lock_guard _(_mx);
        return std::invoke(std::forward<Fn>(fn));
    }

protected:
    std::size_t _tick_counter = 0;
    mutable std::mutex _mx;
    std::vector<std::weak_ptr<StreamClient<T> > > _awaiters;
    std::optional<T> _event;
    

    void broadcast();
};



template<MarketStreamType T>
class StreamClient<T> : public IMarketEventStream<T>, public std::enable_shared_from_this<StreamClient<T> >{
public:

    StreamClient(std::shared_ptr<StreamServer<T> > server):_server(std::move(server)) {}

    using Event = typename IMarketEventStream<T>::Event;
    
    virtual coro::awaitable<Event> read();
    virtual void close();
    void wakeup();

protected:
    std::shared_ptr<StreamServer<T> > _server;
    std::size_t _tick_counter = 0;
    IExecutionWorker::proxy_result<Event> _result;


};



template<MarketStreamType T>
bool StreamServer<T>::await(std::size_t counter, std::shared_ptr<StreamClient<T> > client){
    std::lock_guard _(_mx);
    if (counter < _tick_counter) return false;
    _awaiters.push_back(client);
    return true;
}
template<MarketStreamType T>
typename StreamServer<T>::Event StreamServer<T>::get_last_event(std::size_t &my_counter) const {
    std::lock_guard _(_mx);
    auto cnt = my_counter;
    my_counter = _tick_counter;
    return {
        std::chrono::system_clock::now(),
        _tick_counter - cnt - 1,
        *_event
    };
}
template<MarketStreamType T>
void StreamServer<T>::post(const T &value) noexcept{
    std::unique_lock<std::mutex> _(_mx);
    ++_tick_counter;
    _event = value;    
    broadcast();
}

template<MarketStreamType T>
bool StreamServer<T>::event_ready(std::size_t counter) const {
    std::lock_guard _(_mx);
    return  (counter < _tick_counter);    
}

template<MarketStreamType T>
void StreamServer<T>::post(T &&value) noexcept {
    std::unique_lock<std::mutex> _(_mx);
    ++_tick_counter;
    _event = std::move(value);    
    broadcast();
}

template<MarketStreamType T>
void StreamServer<T>::broadcast() {
    auto cnt = _awaiters.size();
    for (std::size_t i = 0; i < cnt ; ++i) {
        auto awt = _awaiters[i].lock();
        if (awt) {
            awt->wakeup();
        }
    }
    _awaiters.erase(_awaiters.begin(), _awaiters.begin()+cnt);
}

template<MarketStreamType T>
coro::awaitable<typename StreamClient<T>::Event> StreamClient<T>::read(){
    if (_server->event_ready(_tick_counter)) {
        return _server->get_last_event(_tick_counter);
    } else {
        return [this](auto promise) -> coro::prepared_coro {            
            _result = std::move(promise);
            if (_server->await(_tick_counter, this->shared_from_this())) return {};
            wakeup();
            return {};
        };
    }

}
template<MarketStreamType T>
void StreamClient<T>::close(){    
    _server->sync([this]{
        return _result(std::nullopt);
    });    
}
template<MarketStreamType T>
void StreamClient<T>::wakeup(){
    _result(_server->get_last_event(_tick_counter));
}



}