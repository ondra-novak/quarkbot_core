#pragma once

#include "coro/src/basic_coro/awaitable.hpp"
#include "defs.hpp"
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
namespace quarkbot {

class IExecutionWorker {
public:

    virtual ~IExecutionWorker() = default;


    ///Post task to the worker
    /**
        @param fn function to execute in context of the worker
        @note the execution is asynchronous, function returns immediately regardless on
        whether posted function was executed
    */
    template<std::invocable<> Fn>
    requires (std::is_move_constructible_v<Fn>)
    void post(Fn fn) {
        auto exec_fn = [](void *ctx) noexcept {
                Fn *ptr = static_cast<Fn *>(ctx);
                std::invoke(*ptr);
                std::destroy_at(ptr);
        };
        if constexpr(std::is_trivially_copyable_v<Fn>) {
            enqueue(exec_fn, &fn, sizeof(fn));
        } else {
            enqueue(exec_fn, &fn, sizeof(fn), [](void *src, void *trg){
                Fn *src_ptr = static_cast<Fn *>(src);
                Fn *trg_ptr = static_cast<Fn *>(trg);
                std::construct_at(trg_ptr, std::move(*src_ptr));
            });
        }
    }

    ///Create new execution worker
    /**
        In most cases, it starts a new thread. The thread run if there is a reference
        or a work to execute

        @note Backtest probably doesn't spawn a new thread, it simply just creates a new reference
    */
    virtual PExecutionWorker spawn() = 0;


    ///Run a coroutine in this executable worker
    /**
        The coroutine runs in new worker detached from current worker
        @param coro new coroutine

        @note you don't need to use this function to run a coroutine in
        current execution worker. Just call the coroutine as normal function


    */
    void run(coroutine coro) {
        post([coro = std::move(coro)]{});
        
    }

    ///Returns this thread's execution worker
    /**
    @return reference to execution worker, if thread is execution worker,
             otherwise returns nullptr for other threads
     */
    static PExecutionWorker current() {
        return _current_worker.lock();
    }

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
            if (p) _wrk->post([p = std::move(p)]{});
            return {};
        }

    protected:
        coro::awaitable_result<T> _p;
        PExecutionWorker _wrk;
    };


protected:

    using ExecutionFn = void (*)(void *);
    using CreateFn = void (*)(void *, void *);

    virtual void enqueue(ExecutionFn exec_fn, void *closure_ptr,  std::size_t closure_size) = 0;
    virtual void enqueue(ExecutionFn exec_fn, void *closure_ptr,  std::size_t closure_size, CreateFn create_fn) = 0;

    static thread_local std::weak_ptr<IExecutionWorker> _current_worker;

};

inline  thread_local std::weak_ptr<IExecutionWorker> IExecutionWorker::_current_worker;


}