#pragma once

#include "abstract/iexecution_worker.hpp"
#include <concepts>
#include <stdexcept>
#include <stop_token>
namespace quarkbot {

///convenience class for managing single timer from coroutine. RAII object
/** @note object can be used only in context of execution worker

The class uses sleep_for and sleep_until functions of execution worker, 
so it can be used in any context of execution worker. 
It also provides cancel function to interrupt sleep and 
and cancel_and_join function to interrupt sleep and wait until sleeping coroutine finishes.
*/
class Timer {
public:
    
    Timer() = default;
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
        set_worker();
        return _worker->sleep_for(duration, &_cancel_signal);
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
        set_worker();
        return _worker->sleep_until(time_point, &_cancel_signal);
    }


    ///cancel timer object
    /**
        Interrupts any ongoing sleep operation and prevents any future sleep operation. 
        @return true if there was an ongoing sleep operation which was interrupted, false otherwise

        @note if false returned it doesn't necessarily mean that there is no sleeping coroutine,
             it can also mean that sleep operation was completed between the moment when sleep function returned and cancel function was called.
     */
    bool cancel() {
        return _worker  && _worker->cancel(&_cancel_signal);
    }
    
    ///set worker in advance
    /**
        assign current worker as worker for the timer. 
        If there is already worker assigned, function is no-op
        @note not MT safe
    */
    void set_worker() {
        if (_worker) return;
        _worker = IExecutionWorker::current();
        if (!_worker) throw std::runtime_error("Timer can be used only in an exection worker");
    }

    ///change worker
    /**
        assign current worker as worker for the timer. 
        @note not MT safe        
    */
    template<typename _ExecutionWorker>
    requires (requires(_ExecutionWorker v){{v.get_handle()}->std::convertible_to<std::shared_ptr<IExecutionWorker>>;})
    void set_worker(_ExecutionWorker worker) {
        _worker = worker.get_handle();
    }

    Timer &stop_on(const std::stop_source &src) & {
        return stop_on(src.get_token());
    }
    Timer &&stop_on(const std::stop_source &src) && {
        return std::move(*this).stop_on(src.get_token());
    }
    Timer &stop_on( std::stop_token tkn) & {    
        _callback.emplace(std::move(tkn), StopCB{this});
        return *this;
    }
    Timer &&stop_on( std::stop_token tkn) && {
        _callback.emplace(std::move(tkn), StopCB{this});
        return std::move(*this);
    }

    auto now()  {
        set_worker();
        return _worker->now();
    }

protected:    
    struct StopCB {
        Timer *owner;
        void operator()(){owner->cancel();}
    };

    std::shared_ptr<IExecutionWorker> _worker;
    coro::cancel_signal _cancel_signal;
    std::optional<std::stop_callback<StopCB> > _callback;
};



}