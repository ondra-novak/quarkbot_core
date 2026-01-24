#include "thread_worker.hpp"
#include "ifc/defs.hpp"
#include <memory>
#include <stop_token>
#include <thread>

namespace quarkbot {


ThreadWorker::~ThreadWorker() {   
    _stop.request_stop();  
    if (_thr.joinable()) {
        if (_thr.get_id() == std::this_thread::get_id()) {
            _thr.detach();
        } else {
            _thr.join();
        }
    }    
}

PExecutionWorker ThreadWorker::spawn() {
    auto ptr = std::make_shared<ThreadWorker>();
    ptr->start();
    return ptr;
}


void ThreadWorker::start() {    
    _thr = std::thread([this, tkn = _stop.get_token()]{
        worker(std::move(tkn));
    });
}

}