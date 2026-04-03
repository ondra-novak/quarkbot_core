#pragma once

#include <basic_coro/prepared_coro.hpp>
#include "ifc/execution_worker.hpp"
#include "ifc/scheduler.hpp"
#include "ifc/defs.hpp"
#include "impl/scheduler_rt.hpp"
#include <chrono>
#include <memory>
#include <queue>
namespace quarkbot {


class BacktestExecutionWorker: public IScheduler, public IExecutionWorker, public std::enable_shared_from_this<BacktestExecutionWorker> {
public:
    virtual std::chrono::system_clock::time_point now() const override;
    virtual awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) override;
    virtual awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) override;
    virtual void cancel(coro::cancel_signal *cancel_signal) override;
    virtual PExecutionWorker spawn() noexcept override;
    virtual void resume(std::coroutine_handle<> h) noexcept override;
    void set_current_time(std::chrono::system_clock::time_point tp);
    using IExecutionWorker::resume;    
protected:

    std::mutex _mx;
    std::queue<coro::prepared_coro> _microtask_queue;
    ScheduledQueue<ProxyResult<bool>, std::chrono::system_clock::time_point, cancel_signal *> _sch_queue;
    std::atomic<std::chrono::system_clock::time_point> _cur_time;    
};

}