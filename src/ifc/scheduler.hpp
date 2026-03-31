#pragma once

#include "../coro/src/basic_coro/cancel_signal.hpp"
#include "defs.hpp"
#include <chrono>

namespace quarkbot {

class IScheduler {
public:
    using cancel_signal = coro::cancel_signal;

    virtual ~IScheduler() = default;
    ///Get current time from scheduler
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
    virtual awaitable<void> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) = 0;
    ///Sleep for specified duration or until alerted
    /**
        @param duration duration to sleep
        @param cancel_signal_ptr optional pointer to alert flag. If provided, it serves as
        identification and also helps to properly interrupt sleep. Use interrupt function
        to alert the flag. If this flag is set to true when function is called, the function
        returns immediately canceled awaitable.
        @return awaitable which completes when duration elapses or alert flag is set
     */
    virtual awaitable<void> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) = 0;

    ///Interrupt any awaitable sleeping on the scheduler with specified alert flag
    /**
       Atomically cancels sleeping coroutine and sets the flag to true. The flag prevents
       to reenter the sleep if the coroutine calls sleep function again with the same flag. 
       This also serves as a signal to the sleeping coroutine.

     * @param cancel_signal alert flag used to identify sleeping awaitables
     */
    virtual void interrupt(coro::cancel_signal *cancel_signal) = 0;

};

}