#pragma once

#include "basic_coro/scheduler.hpp"
#include "ifc/execution_worker.hpp"
#include <chrono>
#include <memory>

namespace quarkbot {

class BacktestExecutor final: public IExecutionWorker, public std::enable_shared_from_this<BacktestExecutor> {
public:
    virtual void resume(std::coroutine_handle<> h) noexcept override;
    virtual PExecutionWorker spawn() noexcept override;
    virtual std::chrono::system_clock::time_point now() const override;
    virtual awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) override;
    virtual awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) override;
    virtual void cancel(coro::cancel_signal *cancel_signal) override;

    void attach_to_thread();


    void flush_queue();
    void set_time(std::chrono::system_clock::time_point tp);

    
protected:

    coro::manual_scheduler<std::chrono::system_clock::time_point> _scheduler;
    std::queue<coro::prepared_coro> _dispatch_queue;

};

}