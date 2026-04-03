#include "worker_base.hpp" 
#include <algorithm>


namespace quarkbot {


WorkerBase::~WorkerBase() {   
    _stop.request_stop();  
}

PExecutionWorker WorkerBase::spawn() {
    return shared_from_this();
}

void WorkerBase::attach() {
    worker(_stop.get_token());
}

bool WorkerBase::dispatch() {
    PExecutionWorker me;
    std::unique_lock lk(_mx);
    if (!_q.empty()) {
        me = dispatch_item(lk);        
        return true;
    } else {
        return false;
    }
}

PExecutionWorker WorkerBase::dispatch_item(std::unique_lock<std::mutex> &lk) {
    auto &f = _q.front();
    auto me = std::move(f.me);
    lk.unlock();
    f.run(f.closure);                
    lk.lock();
    _q.pop_front();
    return me;

}

void WorkerBase::worker(std::stop_token tkn) {
    std::stop_callback _(tkn,[this]{
        _cv.notify_one();
    });

    _current_worker = this->weak_from_this();
    while (!tkn.stop_requested()) {
        PExecutionWorker me;
        std::unique_lock lk(_mx);
        if (_q.empty()) {
            _cv.wait(lk);
        } else {
            me =  dispatch_item(lk);
        }
    }

}


template<typename CFn>
void WorkerBase::enqueue_internal(ExecutionFn exec_fn,  std::size_t closure_size, CFn create_fn) {
    if (closure_size <= max_closure_size) {
        {
            std::lock_guard _(_mx);
            _q.push_back({});
            auto &b = _q.back();
            create_fn(b.closure);
            b.run = exec_fn;
            b.me = shared_from_this();
        }
        _cv.notify_one();
    } else {
        auto m = std::make_unique<char[]>(closure_size);
        create_fn(m.get());
        this->post([m = std::move(m), exec_fn]() mutable{
            exec_fn(m.get());
        });
    }
}

void WorkerBase::enqueue(ExecutionFn exec_fn, void *closure_ptr,  std::size_t closure_size) {
    enqueue_internal(exec_fn, closure_size, [&](char *target){
        std::copy_n(static_cast<const char *>(closure_ptr), closure_size, target);
    });
}

void WorkerBase::enqueue(ExecutionFn exec_fn, void *closure_ptr,  std::size_t closure_size, CreateFn create_fn) {
    enqueue_internal(exec_fn, closure_size, [&](char *target){
        create_fn(closure_ptr, target);
    });
}

}