#pragma once


#include "basic_coro/cancel_signal.hpp"
#include "../defs.hpp"
#include "quarkbot/types.hpp"
#include <chrono>

namespace quarkbot {

class IExecutionWorker {
public:
    using cancel_signal = coro::cancel_signal;

    virtual ~IExecutionWorker() = default;
    virtual void resume(std::coroutine_handle<> h) noexcept = 0;
    virtual PExecutionWorker spawn() noexcept = 0;
    static PExecutionWorker current() {return _current_worker.lock();}
    virtual std::chrono::system_clock::time_point now() const = 0;
    virtual awaitable<bool> sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr = nullptr) = 0;
    virtual awaitable<bool> sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr = nullptr) = 0;
    virtual bool cancel(coro::cancel_signal *cancel_signal) = 0;
    virtual bool quiesce() = 0;

    class Null; //todo
   
protected:
    static thread_local std::weak_ptr<IExecutionWorker> _current_worker;
};
inline  thread_local std::weak_ptr<IExecutionWorker> IExecutionWorker::_current_worker;

class IExecutionWorker::Null: public IExecutionWorker{
    virtual void resume(std::coroutine_handle<> h) noexcept {h.resume();}
    virtual PExecutionWorker spawn() noexcept {return {};}
    virtual std::chrono::system_clock::time_point now() const {return std::chrono::system_clock::now();}
    virtual awaitable<bool> sleep_until(std::chrono::system_clock::time_point , cancel_signal * = nullptr) {
        return false;
    }
    virtual awaitable<bool> sleep_for(std::chrono::system_clock::duration , cancel_signal * = nullptr) {
        return false;
    }
    virtual bool cancel(coro::cancel_signal *) {
        return false;
    }
    virtual bool quiesce() {
        return false;
    }

};

}