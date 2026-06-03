#include "thread_executor.hpp"
#include "ifc/execution_worker.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace quarkbot {

    void ThreadExecutor::resume(std::coroutine_handle<> h) noexcept {
        auto fin = std::unique_ptr<ThreadExecutor, Notify>(this, Notify{});
        std::scoped_lock _(_mx);
        _dispatch_queue.push(std::move(h));
        manage_lock_me();
    }

    std::shared_ptr<ThreadExecutor> ThreadExecutor::create(){
          return create([](std::coroutine_handle<> h) {h.resume();});
    }        
    
    PExecutionWorker ThreadExecutor::spawn() noexcept {
        return create();
    }
    std::chrono::system_clock::time_point ThreadExecutor::now() const {
        return std::chrono::system_clock::now();
    }
    awaitable<bool> ThreadExecutor::sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr) {
        auto fin = std::unique_ptr<ThreadExecutor, Notify>(this, Notify{});
        std::scoped_lock _(_mx);
        auto r =  _scheduler.sleep_until(time_point, cancel_signal_ptr);
        manage_lock_me();
        return r;
    }
    awaitable<bool> ThreadExecutor::sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr) {
        auto fin = std::unique_ptr<ThreadExecutor, Notify>(this, Notify{});
        std::scoped_lock _(_mx);
        auto r =  _scheduler.sleep_until(now()+duration, cancel_signal_ptr);
        manage_lock_me();
        return r;
    }
    bool ThreadExecutor::cancel(coro::cancel_signal *cancel_signal) {
        std::scoped_lock _(_mx);
        auto out = _scheduler.cancel(cancel_signal);
        if (out) {
            _dispatch_queue.push(std::move(out));
            _cv.notify_one();
            return true;
        } else {
            return false;
        }
    }

  
    ThreadExecutor::~ThreadExecutor() {
        if (_thr.joinable()) {
            _thr.request_stop();
            if (std::this_thread::get_id() == _thr.get_id()) {
                _thr.detach();
            } else {
                _thr.join();
            }
        }        
    }

    void ThreadExecutor::manage_lock_me() {
        bool keeplock = (_scheduler.get_first_scheduled_time() || !_dispatch_queue.empty());
        if (!_lock_me && keeplock) {
            _lock_me = shared_from_this();
        } else if (_lock_me && !keeplock) {
            _lock_me.reset();
        }
    }

}