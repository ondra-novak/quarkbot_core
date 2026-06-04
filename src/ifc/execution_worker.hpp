#pragma once

#include "basic_coro/awaitable.hpp"
#include "basic_coro/cancel_signal.hpp"
#include "basic_coro/result_proxy.hpp"
#include "abstract/iexecution_worker.hpp"
#include <chrono>
#include <cstddef>
#include <stdexcept>
namespace quarkbot {

class ExecutionWorker {
public:

    explicit ExecutionWorker(std::nullptr_t) {}
    ExecutionWorker(std::shared_ptr<IExecutionWorker> worker):_worker(std::move(worker)) {}

    void resume(std::coroutine_handle<> h) noexcept {
        _worker->resume(h);
    }
    void resume(coro::prepared_coro h) {
        if (h) _worker->resume(h.release());
    }
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
    ExecutionWorker spawn() noexcept {
        return _worker->spawn();
    }

    ///Returns this thread's execution worker
    /**
    @return reference to execution worker, if thread is execution worker,
             otherwise returns nullptr for other threads
     */
    static ExecutionWorker current() { return IExecutionWorker::current();}

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
    std::chrono::system_clock::time_point now() const {
        return _worker->now();
    }

    ///Sleep for specified time or until alerted
    /**
        @param time_point time point until which to sleep
        @param cancel_signal_ptr optional pointer to alert flag. If provided, it serves as
        identification and also helps to properly interrupt sleep. Use interrupt function
        to alert the flag. If this flag is set to true when function is called, the function
        returns immediately canceled awaitable.
        It is allowed to have multiple sleeps on single flag. Note that cancel command will cancel all of them
        @return awaitable which completes when time point is reached or alert flag is set
     */
    awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) {
        return _worker->sleep_until(time_point, cancel_signal_ptr);
    }
    ///Sleep for specified duration or until alerted
    /**
        @param duration duration to sleep
        @param cancel_signal_ptr optional pointer to alert flag. If provided, it serves as
        identification and also helps to properly interrupt sleep. Use interrupt function
        to alert the flag. If this flag is set to true when function is called, the function
        returns immediately canceled awaitable.
        It is allowed to have multiple sleeps on single flag. Note that cancel command will cancel all of them
        @return awaitable which completes when duration elapses or alert flag is set
     */
    awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) {
        return _worker->sleep_for(duration, cancel_signal_ptr);
    }

    ///Interrupt any awaitable sleeping on the scheduler with specified alert flag
    /**
       Atomically cancels sleeping coroutine and sets the flag to true. The flag prevents
       to reenter the sleep if the coroutine calls sleep function again with the same flag. 
       This also serves as a signal to the sleeping coroutine.

       @note if there are multiple coroutines on same signal, they are canceled all of them

     * @param cancel_signal alert flag used to identify sleeping awaitables
     */
    bool cancel(coro::cancel_signal *cancel_signal) {
        return _worker->cancel(cancel_signal);
    }

    ExecutionWorker &required() {
        if (!_worker) throw std::runtime_error("Requires execution worker");
        return *this;
    }

    explicit operator bool() const {return static_cast<bool>(_worker);}

    auto get_handle() const {return _worker;}
    
protected:


    std::shared_ptr<IExecutionWorker> _worker;

};




struct ProxyResultExecutor {
    ExecutionWorker _worker{nullptr};
    void operator()(coro::prepared_coro coro) {
        if (_worker) _worker.resume(std::move(coro));        
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


///convenience class for managing single timer from coroutine. RAII object
/** @note object can be used only in context of execution worker

The class uses sleep_for and sleep_until functions of execution worker, 
so it can be used in any context of execution worker. 
It also provides cancel function to interrupt sleep and 
and cancel_and_join function to interrupt sleep and wait until sleeping coroutine finishes.
*/
class Timer {
public:
    
    ///constructor with current execution worker
    /**
        this should be declared in corourine body or in object managed by the coroutine.        
    */
    Timer():_worker(IExecutionWorker::current()) {
        if (!_worker) throw std::runtime_error("Timer can be used only in context of execution worker");
    }
    ///constructor with specified execution worker
    /**
        @param worker execution worker. When coroutine starts sleeping, it wakeup is scheduled on this worker.
    */
    Timer(ExecutionWorker worker):_worker(std::move(worker)) {}
    
    ///non copyable
    Timer(const Timer &) = delete;
    ///non copyable
    Timer &operator=(const Timer &) = delete;



    ///sleep for specified duration or until alerted
    /**
        @param duration duration to sleep
        @return awaitable which completes when duration elapses or alert flag is set
        @retval true sleep successfully completed
        @retval false sleep was interrupted by cancel function

        @note awaitable object must be co_awaited.

        @note when sleep is interrupted by cancel function, the sleep operation is disabled for good, so any subsequent call to sleep functions will return immediately with false. 

        @code
            while (co_await timer.sleep_for(std::chrono::seconds(1))) {
                //do something every second
            }
        @endcode
     */
    awaitable<bool> sleep_for(std::chrono::system_clock::duration duration) {
        return _worker.sleep_for(duration, &_cancel_signal);
    }


    ///sleep until specified time point or until alerted
    /**
        @param time_point time point until which to sleep
        @return awaitable which completes when time point is reached or alert flag is set
        @retval true sleep successfully completed
        @retval false sleep was interrupted by cancel function  
        @note awaitable object must be co_awaited.
        @note when sleep is interrupted by cancel function, the sleep operation is disabled for good, so any subsequent call to sleep functions will return immediately with false. 
    */
    awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point) {
        return _worker.sleep_until(time_point, &_cancel_signal);
    }


    ///cancel timer object
    /**
        Interrupts any ongoing sleep operation and prevents any future sleep operation. 
        @return true if there was an ongoing sleep operation which was interrupted, false otherwise

        @note if false returned it doesn't necessarily mean that there is no sleeping coroutine,
             it can also mean that sleep operation was completed between the moment when sleep function returned and cancel function was called.
     */
    bool cancel() {
        return _worker.cancel(&_cancel_signal);
    }   

protected:
    ExecutionWorker _worker;
    coro::cancel_signal _cancel_signal;
};



}


