#pragma once

#include "basic_coro/cancel_signal.hpp"
#include "ifc/defs.hpp"
#include "ifc/execution_worker.hpp"
#include <atomic>
#include <unistd.h>
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
    Timer(PExecutionWorker worker):_worker(worker) {}
    
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
        return _worker->cancel(&_cancel_signal);
    }   

protected:
    PExecutionWorker _worker;
    coro::cancel_signal _cancel_signal;
};



}