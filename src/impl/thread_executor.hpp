#pragma once

#include <basic_coro/scheduler.hpp>
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include <condition_variable>
#include <coroutine>
#include <memory>
namespace quarkbot {


    class ThreadExecutor : public IExecutionWorker, public std::enable_shared_from_this<ThreadExecutor>{
    public:        
        virtual void resume(std::coroutine_handle<> h) noexcept override;
        virtual PExecutionWorker spawn() noexcept override;
        virtual std::chrono::system_clock::time_point now() const override;
        virtual awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) override;
        virtual awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) override;
        virtual bool cancel(coro::cancel_signal *cancel_signal) override;

        static std::shared_ptr<ThreadExecutor> create();

        template<std::invocable<std::coroutine_handle<> > CoroExecutor>
        static std::shared_ptr<ThreadExecutor> create(CoroExecutor executor) {
            auto x = std::make_shared<ThreadExecutor>();
            x->start(std::move(executor));
            return x;
        }

        

        virtual ~ThreadExecutor();

    protected:
        std::mutex _mx;
        std::condition_variable _cv;
        coro::manual_scheduler<std::chrono::system_clock::time_point> _scheduler;
        std::queue<coro::prepared_coro> _dispatch_queue;
        std::jthread _thr;
        std::shared_ptr<ThreadExecutor> _lock_me;


        struct Notify {
            void operator()(ThreadExecutor *x) {x->_cv.notify_one();};
        };

        template<typename Fn>
        void start(Fn fn) {
            _thr = std::jthread([this, fn = std::move(fn)](std::stop_token tkn) mutable {
                _current_worker = shared_from_this();
                std::stop_callback _(tkn, [&]{_cv.notify_one();});
                std::unique_lock lk(_mx);
                while (!tkn.stop_requested()) {            
                    auto top = _scheduler.get_first_scheduled_time();
                    if (top.has_value()) {
                        auto tp = now();
                        if (tp >= top.value()) {
                            auto r = _scheduler.remove_first();
                            _dispatch_queue.push(r(true));
                            continue;
                        }
                    }
                    //if dispatch queue is not empty
                    if (!_dispatch_queue.empty()) {
                        auto p = std::move(_dispatch_queue.front());
                        auto lkme = _lock_me;
                        _dispatch_queue.pop();
                        manage_lock_me();
                        lk.unlock();
                        fn(p.release());
                        lkme.reset();
                        if (tkn.stop_requested()) return;//exit immediately
                        lk.lock();
                        continue;
                    } 
                    if (top.has_value()) {
                        _cv.wait_until(lk,top.value());
                        continue;
                    }
                    _cv.wait(lk);                    
                }                
            });
        }
       void manage_lock_me();
        void worker(std::stop_token tkn);

    };

}