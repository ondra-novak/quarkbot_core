#pragma once

#include "ifc/scheduler.hpp"
#include "ifc/defs.hpp"
#include "impl/scheduler_rt.hpp"
#include "worker_base.hpp"
#include <chrono>
namespace quarkbot {


class BacktestExecutionWorker: public WorkerBase, public IScheduler {
public:
    virtual std::chrono::system_clock::time_point now() const override;
    virtual awaitable<void> sleep_until(std::chrono::system_clock::time_point time_point, alert_flag *alert_flag_ptr = nullptr) override;
    virtual awaitable<void> sleep_for(std::chrono::system_clock::duration duration, alert_flag *alert_flag_ptr = nullptr) override;
    virtual void interrupt(coro::alert_flag *alert_flag) override;
    virtual PExecutionWorker spawn() override;
    void set_current_time(std::chrono::system_clock::time_point tp);
protected:

    ScheduledQueue<awaitable<void>::result, std::chrono::system_clock::time_point, alert_flag *> _sch_queue;
    std::atomic<std::chrono::system_clock::time_point> _cur_time;
};

}