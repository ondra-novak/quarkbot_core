#pragma once

#include "ifc/scheduler.hpp"
#include "ifc/execution_worker.hpp"
#include "utils/scheduled_queue.hpp"
#include <chrono>
#include <condition_variable>
#include <thread>
namespace quarkbot {

class SchedulerRT: public IScheduler {
public:
    virtual std::chrono::system_clock::time_point now() const override;
    virtual awaitable<void> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr) override;
    virtual awaitable<void> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr) override;
    virtual void interrupt(coro::cancel_signal *cancel_signal) override;

    void start();


protected:

    using Queue = ScheduledQueue<IExecutionWorker::proxy_result<void>, 
                   std::chrono::system_clock::time_point,
                   cancel_signal *>;

                   
    Queue _queue;
    std::mutex _mx;
    std::condition_variable _cv;
    std::jthread _thr;


};




}

