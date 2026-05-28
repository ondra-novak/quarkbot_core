#pragma once

#include <basic_coro/scheduler.hpp>
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include <condition_variable>
#include <memory>
namespace quarkbot {


    class ThreadExecutor final: public IExecutionWorker, public std::enable_shared_from_this<ThreadExecutor>{
    public:        
        virtual void resume(std::coroutine_handle<> h) noexcept override;
        virtual PExecutionWorker spawn() noexcept override;
        virtual std::chrono::system_clock::time_point now() const override;
        virtual awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) override;
        virtual awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) override;
        virtual bool cancel(coro::cancel_signal *cancel_signal) override;

        static std::shared_ptr<ThreadExecutor> create();

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

        void start();
        void manage_lock_me();
        void worker(std::stop_token tkn);

    };

}