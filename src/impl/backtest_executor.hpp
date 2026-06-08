#pragma once

#include "basic_coro/scheduler.hpp"
#include "ifc/execution_worker.hpp"
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
    */
    static std::shared_ptr<BacktestExecutor> create();

    virtual void resume(std::coroutine_handle<> h) noexcept override;
    virtual PExecutionWorker spawn() noexcept override;
    virtual std::chrono::system_clock::time_point now() const override;
    virtual awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) override;
    virtual awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) override;
    virtual bool cancel(coro::cancel_signal *cancel_signal) override;
    virtual void join() override;
   

    void flush_queue();
    void set_time(std::chrono::system_clock::time_point tp);
    //sets initial time (without flushing loop)
    void set_initial_time(std::chrono::system_clock::time_point tp);
    bool empty() const ;



protected:
    BacktestExecutor() = default;

    coro::manual_scheduler<std::chrono::system_clock::time_point> _scheduler;
    std::queue<coro::prepared_coro> _dispatch_queue;

};

}