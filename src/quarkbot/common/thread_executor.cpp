#include "thread_executor.hpp"
#include "basic_coro/prepared_coro.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace quarkbot {

    void ThreadExecutor::resume(std::coroutine_handle<> h) noexcept {
        {
            std::scoped_lock _(_mx);
            _dispatch_queue.push(std::move(h));
            manage_lock_me();
        }
        _cv.notify_one();
    }

    std::shared_ptr<ThreadExecutor> ThreadExecutor::create(){
            auto x = std::make_shared<ThreadExecutor>();
            x->start();
            return x;
        }
    PExecutionWorker ThreadExecutor::spawn() noexcept {
        return create();
    }
    std::chrono::system_clock::time_point ThreadExecutor::now() const {
        return std::chrono::system_clock::now();
    }
    awaitable<bool> ThreadExecutor::sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr) {
        return [=,this](auto promise) -> coro::prepared_coro{
            std::scoped_lock _(_mx);
            auto top = _scheduler.get_first_scheduled_time();
            if (cancel_signal_ptr && cancel_signal_ptr->is_canceled())  return promise(false);
            _scheduler.schedule_at(std::move(promise), time_point, cancel_signal_ptr);
            manage_lock_me();
            if (!top.has_value() || *top > time_point) {
                _cv.notify_one();
            }
            return {};
        };
    }
    awaitable<bool> ThreadExecutor::sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr) {
        return ThreadExecutor::sleep_until(ThreadExecutor::now()+duration, cancel_signal_ptr);
    }

    bool ThreadExecutor::cancel(coro::cancel_signal *cancel_signal) {
        if (cancel_signal) {
            std::scoped_lock _(_mx);
            cancel_signal->request_cancel();
            bool ok = false;            
            auto r = _scheduler.remove_by_ident(cancel_signal);
            while (r) {
                ok = true;
                _dispatch_queue.push(r(false).symmetric_transfer());
                _cv.notify_one();
                r = _scheduler.remove_by_ident(cancel_signal);
            }
            return ok;
        }
        return false;
    }

    void ThreadExecutor::start() {
        _thr = std::jthread([this](std::stop_token tkn){
            worker(tkn);
        });

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

    void ThreadExecutor::worker(std::stop_token tkn) {
        _current_worker = shared_from_this();
        std::stop_callback _(tkn, [&]{_cv.notify_one();});
        //lock internals
        std::unique_lock lk(_mx);
        while (!tkn.stop_requested()) {            
            //check for top time
            auto top = _scheduler.get_first_scheduled_time();
            //is top defined
            if (top.has_value()) {
                //check current time
                auto tp = now();
                //if due time
                if (tp >= top.value()) {
                    //remove top item, resolve it and schedule execution into queue
                    auto r = _scheduler.remove_first();
                    _dispatch_queue.push(r(true));                        
                    //cycle
                    continue;
                }
            }
            //if dispatch queue is not empty
            if (!_dispatch_queue.empty()) {
                //pick front coroutine
                auto p = std::move(_dispatch_queue.front());
                //save _lock_me to keep locked during execution
                auto lkme = _lock_me;
                //remove from dispatch queue
                _dispatch_queue.pop();
                //manage lock me state (can be release, but we holding reference)
                manage_lock_me();
                //unlock internals
                _in_task = true;
                lk.unlock();
                //resume coroutine outside lock (also install lazy resume framework)
                p.lazy_resume();
                //destroy thread executor, if there are no references
                lkme.reset();
                //tkn can be signalized especially when this thread performed destruction - this is no longer valid
                if (tkn.stop_requested()) return;//exit immediately
                //lock back
                lk.lock();
                _in_task = false;
                _counter.fetch_add(1, std::memory_order_relaxed);
                _counter.notify_all();
                continue;
            } 
            //queue is empty, but there is top time
            if (top.has_value()) {
                //wait until top time is reached
                _cv.wait_until(lk,top.value());
                continue;
            }
            //wait with no timeout -  until notify
            _cv.wait(lk);
            
        }
        
    }

    bool ThreadExecutor::join() {
        if (std::this_thread::get_id() == _thr.get_id()) {
            std::unique_lock lk(_mx);
            std::size_t sz =   _dispatch_queue.size();
            if (sz == 0) return false;
            while (sz > 0) {
                auto h = std::move(_dispatch_queue.front());
                _dispatch_queue.pop();
                lk.unlock();
                h.resume();
                lk.lock();
                sz--;
            }
            return true;

        } else {
            std::size_t join_counter;
            {
                std::scoped_lock _(_mx);        
                std::size_t sz =  _in_task?1:0 + _dispatch_queue.size();
                if (sz == 0) return false;
                join_counter = _counter.load(std::memory_order_relaxed) + sz;
            }
            auto c = _counter.load(std::memory_order_relaxed);        
            while (c < join_counter) {
                _counter.wait(c);
                c = _counter.load(std::memory_order_relaxed);
            }
            return true;
        }
    }

}