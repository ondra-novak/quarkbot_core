#pragma once

#include "ifc/execution_worker.hpp"
#include <condition_variable>
#include <coroutine>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
namespace quarkbot {

class ThreadWorker: public IExecutionWorker, public std::enable_shared_from_this<ThreadWorker> {
public:
    virtual ~ThreadWorker() override;
    virtual PExecutionWorker spawn() noexcept override ;

    void start();

    virtual void resume(std::coroutine_handle<> h)  noexcept override;
    using IExecutionWorker::resume;



protected:
    std::jthread _thr;
    std::condition_variable _cv;
    std::mutex _mx;
    std::queue<std::coroutine_handle<> > _queue;
    std::shared_ptr<ThreadWorker> self_ref = {};

    void worker(std::stop_token tkn);

    

};



}