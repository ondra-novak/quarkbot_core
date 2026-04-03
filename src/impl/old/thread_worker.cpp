#include "thread_worker.hpp"
#include "ifc/defs.hpp"
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace quarkbot {


ThreadWorker::~ThreadWorker() {   
    if (_thr.joinable()) {
        _thr.request_stop();  
        if (_thr.get_id() == std::this_thread::get_id()) {
            _thr.detach();
        } else {
            _thr.join();
        }
    }    
}

PExecutionWorker ThreadWorker::spawn() noexcept {
    auto ptr = std::make_shared<ThreadWorker>();
    ptr->start();
    return ptr;
}


void ThreadWorker::start() {    
    _thr = std::jthread([this](std::stop_token tkn){
        worker(std::move(tkn));
    });
}

void ThreadWorker::worker(std::stop_token tkn) {
    std::stop_callback stpcb(tkn, [&]{
        _cv.notify_one();
    });
    while (!tkn.stop_requested()) {
        std::unique_lock lk(_mx);
        if (tkn.stop_requested()) break;
        if (_queue.empty()) {
            _cv.wait(lk);
        } else {
            auto itm = std::move(_queue.front());            
            _queue.pop();
            auto last_hold = _queue.empty()?std::move(self_ref):std::shared_ptr<ThreadWorker>();
            lk.unlock();
            itm.resume();
        }
    }
}

void ThreadWorker::resume(std::coroutine_handle<> h)  noexcept {
    std::lock_guard _(_mx);
    self_ref = shared_from_this();
    _queue.push(h);
    _cv.notify_one();
}

}