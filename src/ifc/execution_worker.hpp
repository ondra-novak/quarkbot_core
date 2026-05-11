#pragma once

#include <basic_coro/awaitable.hpp>
#include <basic_coro/prepared_coro.hpp>
#include <basic_coro/result_proxy.hpp>
#include "basic_coro/cancel_signal.hpp"
#include "defs.hpp"
#include <chrono>
#include <coroutine>
#include <exception>
#include <memory>
#include <stdexcept>
namespace quarkbot {

class IExecutionWorker {
public:

    virtual ~IExecutionWorker() = default;

    virtual void resume(std::coroutine_handle<> h) noexcept = 0;
    void resume(coro::prepared_coro h) {resume(h.release());}
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

    ///Schedule current coroutine (StrategyFragment) on this execution worker
    /**
    Execution is transfered to new execution worker. It can be called from thread which has no execution worker
     */
    awaitable<void> schedule() {
        return [this](auto promise) {
            resume(promise());
        };
    }

    using cancel_signal = coro::cancel_signal;

    ///Get current time 
    /**
        @return current time point
     */
    virtual std::chrono::system_clock::time_point now() const = 0;

    ///Sleep for specified time or until alerted
    /**
        @param time_point time point until which to sleep
        @param cancel_signal_ptr optional pointer to alert flag. If provided, it serves as
        identification and also helps to properly interrupt sleep. Use interrupt function
        to alert the flag. If this flag is set to true when function is called, the function
        returns immediately canceled awaitable.
        @return awaitable which completes when time point is reached or alert flag is set
     */
    virtual awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) = 0;
    ///Sleep for specified duration or until alerted
    /**
        @param duration duration to sleep
        @param cancel_signal_ptr optional pointer to alert flag. If provided, it serves as
        identification and also helps to properly interrupt sleep. Use interrupt function
        to alert the flag. If this flag is set to true when function is called, the function
        returns immediately canceled awaitable.
        @return awaitable which completes when duration elapses or alert flag is set
     */
    virtual awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) = 0;

    ///Interrupt any awaitable sleeping on the scheduler with specified alert flag
    /**
       Atomically cancels sleeping coroutine and sets the flag to true. The flag prevents
       to reenter the sleep if the coroutine calls sleep function again with the same flag. 
       This also serves as a signal to the sleeping coroutine.

     * @param cancel_signal alert flag used to identify sleeping awaitables
     */
    virtual void cancel(coro::cancel_signal *cancel_signal) = 0;
    
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
        if (!this->_executor._worker) {
            this->_result(std::make_exception_ptr(std::runtime_error("Function can be called only from executor worker")));
        }
    }
    ResultAndExecWorker():coro::result_proxy<coro::awaitable_result<T>, ProxyResultExecutor>({},{}) {}
    operator bool() const {return this->_result.operator bool();}
};




}