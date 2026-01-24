#pragma once

#include "ifc/execution_worker.hpp"
#include "ifc/defs.hpp"
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace quarkbot {

    class WorkerBase: public IExecutionWorker ,public std::enable_shared_from_this<WorkerBase> {
public:
    virtual ~WorkerBase();
    virtual PExecutionWorker spawn();

    void attach();
    bool dispatch();

    virtual void enqueue(ExecutionFn exec_fn, void *closure_ptr,  std::size_t closure_size);
    virtual void enqueue(ExecutionFn exec_fn, void *closure_ptr,  std::size_t closure_size, CreateFn create_fn);
protected:
    std::stop_source _stop;
    std::mutex _mx;
    std::condition_variable _cv;
    static constexpr std::size_t max_closure_size =  sizeof(void *)*8-sizeof(ExecutionFn);
    
    struct Item {
        PExecutionWorker me = {};
        ExecutionFn run = {};
        char closure[max_closure_size] = {};
    };

    std::deque<Item> _q;

    template<typename CFn>
    void enqueue_internal(ExecutionFn exec_fn,  std::size_t closure_size, CFn create_fn);

    void worker(std::stop_token tkn);

    PExecutionWorker dispatch_item(std::unique_lock<std::mutex> &lk);

};

}