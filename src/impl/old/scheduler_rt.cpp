#include "scheduler_rt.hpp"
#include <basic_coro/awaitable.hpp>
#include <basic_coro/prepared_coro.hpp>
#include "ifc/defs.hpp"
#include <chrono>
#include <mutex>
#include <stop_token>
#include <thread>

namespace quarkbot {


std::chrono::system_clock::time_point SchedulerRT::now() const{
    return std::chrono::system_clock::now();

}
awaitable<bool> SchedulerRT::sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr){
    return [&](coro::awaitable_result<bool> p) mutable {
        std::lock_guard _(_mx);
        if (cancel_signal_ptr && *cancel_signal_ptr) {
            p(false);
        } else {
            _queue.push(ProxyResult(std::move(p)), time_point, cancel_signal_ptr);            
        }        
    };
}
awaitable<bool> SchedulerRT::sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr){
    return sleep_until(std::chrono::system_clock::now()+duration, cancel_signal_ptr);
}
void SchedulerRT::cancel(coro::cancel_signal *cancel_signal){
    std::lock_guard _(_mx);
    auto iter = _queue.find(cancel_signal);
    if (cancel_signal) cancel_signal->request_cancel();
    if (iter != _queue.end()) {
        _queue.mutable_ref(iter)->value(std::nullopt);
        _queue.erase(iter);
    }    
}
void SchedulerRT::start(){
    _thr = std::jthread([this](std::stop_token tkn){
        std::stop_callback _(tkn, [this]{
            _cv.notify_all();
        });
        while (!tkn.stop_requested()) {
            std::unique_lock lk(_mx);
            if (_queue.empty()) {
                _cv.wait(lk);
            } else {
                auto tm = _queue.top().time;
                auto now = std::chrono::system_clock::now();
                if (now >= tm) {
                    auto c =std::move(_queue.top().value);
                    _queue.pop();
                    c();
                } else {
                    _cv.wait_until(lk, tm);
                }
            }   
        }
    });
}



}