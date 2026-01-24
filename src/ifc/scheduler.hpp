#pragma once

#include "../coro/src/basic_coro/alert_flag.hpp"
#include "defs.hpp"
#include <chrono>

namespace quarkbot {

class IScheduler {
public:
    using alert_flag = coro::alert_flag;

    virtual ~IScheduler() = default;
    ///Get current time from scheduler
    /**
        @return current time point
     */
    virtual std::chrono::system_clock::time_point now() const = 0;

    ///Sleep for specified time or until alerted
    /**
        @param time_point time point until which to sleep
        @param alert_flag_ptr optional pointer to alert flag. If provided, it serves as
        identification and also helps to properly interrupt sleep. Use interrupt function
        to alert the sleep. If this flag is set to true when function is called, the function
        returns immediately canceled awaitable.
        @return awaitable which completes when time point is reached or alert flag is set
     */
    virtual awaitable<void> sleep_until(std::chrono::system_clock::time_point time_point, alert_flag *alert_flag_ptr = nullptr) = 0;
    ///Sleep for specified duration or until alerted
    /**
        @param duration duration to sleep
        @param alert_flag_ptr optional pointer to alert flag. If provided, it serves as
        identification and also helps to properly interrupt sleep. Use interrupt function
        to alert the sleep. If this flag is set to true when function is called, the function
        returns immediately canceled awaitable.
        @return awaitable which completes when duration elapses or alert flag is set
     */
    virtual awaitable<void> sleep_for(std::chrono::system_clock::duration duration, alert_flag *alert_flag_ptr = nullptr) = 0;

    ///Interrupt any awaitable sleeping on the scheduler with specified alert flag
    /**
       Atomically cancels sleeping coroutine and sets the flag to true. The flag prevents
       to reenter the sleep if the coroutine calls sleep function again with the same flag. 
       This also serves as a signal to the sleeping coroutine.

     * @param alert_flag alert flag used to identify sleeping awaitables
     */
    virtual void interrupt(coro::alert_flag *alert_flag) = 0;

};

}