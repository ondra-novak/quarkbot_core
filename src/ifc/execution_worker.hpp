#pragma once

#include "coro/src/basic_coro/awaitable.hpp"
#include "coro/src/basic_coro/prepared_coro.hpp"
#include "defs.hpp"
#include <coroutine>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
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

    ///ensures that coroutine is woken up through a current dispatcher. Throws exception, is there is no dispatcher
    template<typename T>
    class proxy_result {
    public:

        proxy_result()  = default;

        proxy_result(coro::awaitable_result<T> p):_p(std::move(p)),_wrk(IExecutionWorker::current()) {
            if (_wrk == nullptr) {
                throw std::runtime_error("Operation must be called from execution worker");
            }
        }

        operator bool() const {
            return static_cast<bool>(_p);
        }

        template<typename ... Ts>
        requires (std::is_invocable_v<coro::awaitable_result<T>, Ts...>)
        coro::prepared_coro operator()(Ts && ... args) {
            auto p = _p(std::forward<Ts>(args)...);
            if (p) _wrk->resume(std::move(p));
            return {};
        }

    protected:
        coro::awaitable_result<T> _p;
        PExecutionWorker _wrk;
    };


protected:


    static thread_local std::weak_ptr<IExecutionWorker> _current_worker;

};

inline  thread_local std::weak_ptr<IExecutionWorker> IExecutionWorker::_current_worker;


}