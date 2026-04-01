#pragma once

#include <basic_coro/awaitable.hpp>
#include <basic_coro/prepared_coro.hpp>
#include "basic_coro/result_proxy.hpp"
#include "defs.hpp"
#include <coroutine>
#include <memory>
#include <stdexcept>
namespace quarkbot {

class IExecutionWorker {
public:

    virtual ~IExecutionWorker() = default;

    virtual void resume(std::coroutine_handle<> h) noexcept = 0;
    void resume(coro::prepared_coro h) {h.release();}
    ///Run a coroutine in this executable worker
    /**
        The coroutine runs in new worker detached from current worker
        @param coro new coroutine

        @note you don't need to use this function to run a coroutine in
        current execution worker. Just call the coroutine as normal function
    */
    void run(coroutine coro) {
        resume(coro.release());
        
    }
    ///Create new execution worker
    /**
        In most cases, it starts a new thread. The thread run if there is a reference
        or a work to execute

        @note Backtest probably doesn't spawn a new thread, it simply just creates a new reference
    */
    virtual PExecutionWorker spawn() noexcept = 0;

    ///Returns this thread's execution worker
    /**
    @return reference to execution worker, if thread is execution worker,
             otherwise returns nullptr for other threads
     */
    static PExecutionWorker current() {
        return _current_worker.lock();
    }

    
protected:


    static thread_local std::weak_ptr<IExecutionWorker> _current_worker;

};

inline  thread_local std::weak_ptr<IExecutionWorker> IExecutionWorker::_current_worker;


struct ProxyResultExecutor {
    PExecutionWorker _worker;
    void operator()(coro::prepared_coro coro) {
        if (_worker) _worker->resume(std::move(coro));        
    }
};


template<typename T>
class ResultAndExecWorker : public coro::result_proxy<coro::awaitable_result<T>, ProxyResultExecutor> {
public:
    ResultAndExecWorker(coro::awaitable_result<T> res):coro::result_proxy<coro::awaitable_result<T>, ProxyResultExecutor>(
        std::move(res), {IExecutionWorker::current()})
    {
        if (!this->_executor._worker) throw std::runtime_error("Function can be called only from executor worker");
    }
    ResultAndExecWorker():coro::result_proxy<coro::awaitable_result<T>, ProxyResultExecutor>({},{}) {}
    operator bool() const {return this->_result.operator bool();}
};




}