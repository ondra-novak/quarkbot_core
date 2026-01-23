#include "stream_impl.hpp"
#include "coro/src/basic_coro/prepared_coro.hpp"
#include "ifc/defs.hpp"
#include <chrono>
#include <mutex>
#include <optional>

namespace quarkbot {
    

template<StreamType T>
bool StreamServer<T>::await(std::size_t counter, std::shared_ptr<StreamClient<T> > client){
    std::lock_guard _(_mx);
    if (counter < _tick_counter) return false;
    _awaiters.push_back(client);
    return true;
}
template<StreamType T>
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
template<StreamType T>
void StreamServer<T>::post(const T &value){
    std::unique_lock<std::mutex> _(_mx);
    ++_tick_counter;
    _event = value;    
    broadcast();
}

template<StreamType T>
bool StreamServer<T>::event_ready(std::size_t counter) const {
    std::lock_guard _(_mx);
    return  (counter < _tick_counter);    
}

template<StreamType T>
void StreamServer<T>::post(T &&value){
    std::unique_lock<std::mutex> _(_mx);
    ++_tick_counter;
    _event = std::move(value);    
    broadcast();
}

template<StreamType T>
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

template<StreamType T>
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
template<StreamType T>
void StreamClient<T>::close(){
    _server->sync([this]{
        _result(std::nullopt);
    });    
}
template<StreamType T>
void StreamClient<T>::wakeup(){
    _result(_server->get_last_event(_tick_counter));
}


struct Test: StreamTypeItem {
    int x;
};

template class StreamClient<Test>;
template class StreamServer<Test>;

}