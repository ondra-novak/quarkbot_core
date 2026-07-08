#pragma once

#include "abstract/iexecution_worker.hpp"
#include "basic_coro/coroutine.hpp"
#include "basic_coro/pending.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "quarkbot/defs.hpp"


#include <basic_coro/await_proxy.hpp>
#include <basic_coro/awaitable.hpp>
#include <basic_coro/cancel_signal.hpp>
#include <basic_coro/concepts.hpp>
#include <basic_coro/coro_frame.hpp>
#include <basic_coro/pending.hpp>
#include <basic_coro/result_proxy.hpp>
#include <basic_coro/sync_await.hpp>
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

    template<typename T>
    auto launch(coro::coroutine<T> &&coro) {
        return coro.launch([&](coro::prepared_coro c)  {resume(std::move(c));});
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

    ///Join all previously added tasks with current thread
    /**
      Blocks execution until all tasks enqueued before join is called are finished.
      Tasks added after join are not included

      @note it only garantees that tasks enqueued before join are finished, It doesn't
      mean, that tasks enqueued after join will be still queued (as they can be finished
      as well)
      
      @note also works correctly in backtest executor, which invokes flush of the queue
      @retval true some tasks has been processed before join finished
      @retval false no-op, nothing to be processed, queue is empty

     */
    bool join() {
        if (_worker) return _worker->join();
        return false;
    }


    ///Ensures that worker is available - otherwise it throw exception
    ExecutionWorker &required() {
        if (!_worker) throw std::runtime_error("Operation is executed in a thread wich is not Execution Worker. This is required.");
        return *this;
    }

    ///test validity
    explicit operator bool() const {return static_cast<bool>(_worker);}

    ///get handle
    auto get_handle() const {return _worker;}
    
protected:


    std::shared_ptr<IExecutionWorker> _worker;

};




}


