#pragma once

#include <basic_coro/scheduler.hpp>
#include "basic_coro/awaitable.hpp"
#include "basic_coro/cancel_signal.hpp"
#include "quarkbot/abstract/iexecution_worker.hpp"
#include "quarkbot/utils/function_view.hpp"
#include <condition_variable>
#include <coroutine>
#include <memory>
namespace quarkbot {


    class ThreadExecutor : public IExecutionWorker, public std::enable_shared_from_this<ThreadExecutor>{
    public:        
        virtual void resume(std::coroutine_handle<> h) noexcept override;
        virtual void resume_idle(std::coroutine_handle<> h) noexcept override;
        virtual PExecutionWorker spawn() noexcept override;
        virtual std::chrono::system_clock::time_point now() const override;
        virtual awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) override;
        virtual awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) override;
        virtual bool cancel(coro::cancel_signal *cancel_signal) override;
        virtual bool quiesce() override;

        ///create exectuion worker - create new thread
        static std::shared_ptr<ThreadExecutor> create();

        ///Attach current thread
        /**
            @param startupFn function called before the thread is started. It receives instance of the executor. The
            executor not yet running, so function must avoid to run blocking functions. It can spawn
            coroutines, they will continue in the worker. 

            The function exits, when last instance of the worker is removed, and all tasks are finished (including sheduled)
        */
        static void attach(function_view<void(std::shared_ptr<ThreadExecutor>) >  startupFn);

        virtual ~ThreadExecutor();

    protected:
        std::mutex _mx;
        std::condition_variable _cv;
        coro::generic_scheduler<coro::awaitable<bool>::result, std::chrono::system_clock::time_point, coro::cancel_signal *> _scheduler;
        std::queue<coro::prepared_coro> _dispatch_queue;
        std::queue<coro::prepared_coro> _idle_queue;
        std::thread _thr;
        std::stop_source _stpsrc;
        std::shared_ptr<ThreadExecutor> _lock_me;
        std::atomic<std::size_t> _counter;
        bool _in_task = false;

        void start();
        void manage_lock_me();
        void worker(std::stop_token tkn);

    };

}