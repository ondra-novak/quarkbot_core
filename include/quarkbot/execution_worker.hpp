#pragma once

#include "abstract/iexecution_worker.hpp"
#include "basic_coro/coroutine.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "defs.hpp"
#include "utils/wrapper.hpp"


#include <basic_coro/await_proxy.hpp>
#include <basic_coro/awaitable.hpp>
#include <basic_coro/cancel_signal.hpp>
#include <basic_coro/concepts.hpp>
#include <basic_coro/coro_frame.hpp>
#include <basic_coro/pending.hpp>
#include <basic_coro/result_proxy.hpp>
#include <basic_coro/sync_await.hpp>
#include <chrono>
#include <stdexcept>
namespace quarkbot {

class ExecutionWorker: public Wrapper<IExecutionWorker> {
public:

    using Wrapper<IExecutionWorker>::Wrapper;

    class Awaitable_Reschedule : public awaitable<void> {
    public:
        struct no_transform_awaiter{};
        using awaitable<void>::awaitable;
    };
    

    ///Schedule coroutine for resumption in this worker
    /**
        @param h handle of coroutine
        @note low-level
        @note doesn't check for handle validity!
        
    */
    void resume(std::coroutine_handle<> h) noexcept {
        _ptr->resume(h);
    }
    ///Schedule coroutine for resumption in this worker
    /**
        @param h prepared_coro object 
        @note if no coroutine prepared in the object, it does nothing. Expects valid handle in the object
    */
    void resume(coro::prepared_coro h) {
        if (h) _ptr->resume(h.release());
    }
    ///Schedule coroutine for resumption in this worker during idle stage
    /**
        @param h prepared_coro object 
        @note if no coroutine prepared in the object, it does nothing. Expects valid handle in the object
    */
    void resume_idle(coro::prepared_coro h) {
        if (h) _ptr->resume_idle(h.release());
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

    ///Run a coroutine in this worker, return pending object to synchronize later
    /**
        @param coroutine
        @return coro::pending - this object must be synchronized before it is destroyed. Failing this rule causes termination

        @code
        {
            auto p = worker.launch(some_coro(...));
            ...
            co_await p;
        }
        @endcode
    */
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
        auto r= _ptr->spawn();
        if (r) [[likely]] return ExecutionWorker{r};
        return {};
    }

    ///Returns this thread's execution worker
    /**
    @return reference to execution worker, if thread is execution worker,
             otherwise returns nullptr for other threads
     */
    static ExecutionWorker current() { 
        auto r =IExecutionWorker::current();
        if (r) [[likely]] return ExecutionWorker{r};
        return {};
    }        

    ///Schedule current coroutine (StrategyFragment) on this execution worker
    /**
    Execution is transfered to new execution worker. It can be called from thread which has no execution worker
    @note not useful for StrategyFragment - this coroutine must be started in ExecutionWorker
     */
    Awaitable_Reschedule schedule() {
        return [this](auto promise) {
            resume(promise());
        };
    }

    ///postpone execition until the execution worker is idle
    /**
        Idle state is defined as a stage, when all tasks has been executed for current event. 
        For thread executor, this happens before the thread is suspended on wait or wait_until.
        For backtest executor, this happens after current queue is flushed.

        The idle queue is flushed once, so the task can call sleep_until_idle() to schedule on next idle event. 
    */
    Awaitable_Reschedule sleep_until_idle() {
        return [this](auto promise) {
            this->resume_idle(promise());
        };
    }

    using cancel_signal = coro::cancel_signal;

    ///Get current time 
    /**
        @return current time point
     */
    std::chrono::system_clock::time_point now() const {
        return _ptr->now();
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

        @note Use Timer if you can
        @see Timer
     */
    awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) {
        return _ptr->sleep_until(time_point, cancel_signal_ptr);
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

        @note Use Timer if you can
        @see Timer
    */
    awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) {
        return _ptr->sleep_for(duration, cancel_signal_ptr);
    }

    ///Interrupt any awaitable sleeping on the scheduler with specified alert flag
    /**
       Atomically cancels sleeping coroutine and sets the flag to true. The flag prevents
       to reenter the sleep if the coroutine calls sleep function again with the same flag. 
       This also serves as a signal to the sleeping coroutine.

       @note if there are multiple coroutines on same signal, they are canceled all of them

     * @param cancel_signal alert flag used to identify sleeping awaitables

       @note Use Timer if you can
       @see Timer

     */
    bool cancel(coro::cancel_signal *cancel_signal) {
        return _ptr->cancel(cancel_signal);
    }

    ///Wait until all scheduled tasks are completed
    /**
        Waits until all scheduled tasks at time of call are completed. It doesn't wait for tasks scheduled after the call.
        It garantees that all tasks scheduled before the call are completed, but state of tasks scheduled after the call 
            is not guaranteed. It can be completed or not completed, depending on scheduling and execution order.
        @note it also works in backtest executor which calls flush_queue() to execute all scheduled tasks
        
        @note implementation should be safe if called from the thread of the execution worker.
              It just executes all scheduled tasks and returns. It doesn't wait for tasks scheduled after the call.

        @return true if there were tasks to wait for, false if there were no tasks

        @note you cannot synchronize with idle tasks. 
     */
    bool quiesce() {
        return _ptr->quiesce();
    }


    ///Ensures that worker is available - otherwise it throw exception
    ExecutionWorker &required() {
        if (!static_cast<bool>(*this)) {
            throw std::runtime_error("Operation requires an active Execution Worker context, but the current thread is not associated with any Execution Worker.");
        }
        return *this;
    }

};




}


