#pragma once

#include <basic_coro/scheduler.hpp>
#include "basic_coro/awaitable.hpp"
#include "basic_coro/cancel_signal.hpp"
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include <condition_variable>
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
        virtual void join() override;

        static std::shared_ptr<ThreadExecutor> create();

        virtual ~ThreadExecutor();

    protected:
        std::mutex _mx;
        std::condition_variable _cv;
        coro::generic_scheduler<coro::awaitable<bool>::result, std::chrono::system_clock::time_point, coro::cancel_signal *> _scheduler;
        std::queue<coro::prepared_coro> _dispatch_queue;
        std::jthread _thr;
        std::shared_ptr<ThreadExecutor> _lock_me;
        std::atomic<std::size_t> _counter;
        bool _in_task = false;

        void start();
        void manage_lock_me();
        void worker(std::stop_token tkn);

    };

}