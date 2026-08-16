#pragma once

#include "basic_coro/scheduler.hpp"
#include "quarkbot/abstract/iexecution_worker.hpp"
#include <chrono>
#include <memory>

namespace quarkbot {

class BacktestExecutor : public IExecutionWorker, public std::enable_shared_from_this<BacktestExecutor> {
public:

    ///Create execution worker
    /**
    @return execution worker attached to current thread directly as BacktestExecutor shared ptr.
    @note because backtest executor doesn't use threads for workers, this function only creates
    one executor worker per thread. Futher calls of this function causes returning the same object 
    
    Executor must by manually controlled. Check functions flush_queue, and set_time
    */
    static std::shared_ptr<BacktestExecutor> create();

    virtual void resume(std::coroutine_handle<> h) noexcept override;
    virtual void resume_idle(std::coroutine_handle<> h) noexcept override;
    virtual PExecutionWorker spawn() noexcept override;
    virtual std::chrono::system_clock::time_point now() const override;
    virtual awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) override;
    virtual awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) override;
    virtual bool cancel(coro::cancel_signal *cancel_signal) override;
    virtual bool quiesce() override;
   

    ///Flush all item in current queue
    /**
        Flushes queue for all tasks. When task is postopned by schedule() it is also resumed later in the same cycle.
        Also flushes all idle tasks, however if they are postponed, they are resumed by next flush_queue()
    */
    void flush_queue();
    ///Set current time processing all scheduled task and also flushes the queue
    void set_time(std::chrono::system_clock::time_point tp);  

    ///advance time towards time point
    /**
        Difference between set_time and advance_time is, that advance_time don't need to reach the time, it
        will process one time event and stop

        @param tp target time
        @retval true reached and all task scheduled for this time point was executed
        @retval false not reached yet (still processing events)
    */
    bool advance_time(std::chrono::system_clock::time_point tp);

    ///returns true, if empty (task scheduled to exact time are not counted)
    bool empty() const ;



protected:

    coro::manual_scheduler<std::chrono::system_clock::time_point> _scheduler;
    std::queue<coro::prepared_coro> _dispatch_queue;
    std::queue<coro::prepared_coro> _idle_queue;

};

}