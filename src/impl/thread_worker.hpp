#pragma once

#include "impl/worker_base.hpp"
namespace quarkbot {

class ThreadWorker: public WorkerBase {
public:
    virtual ~ThreadWorker();
    virtual PExecutionWorker spawn();

    void start();
protected:
    std::thread _thr;

};



}