#pragma once
#include "basic_coro/coroutine.hpp"
#include "ifc/memory.hpp"
namespace quarkbot {


    ///Asynchronous function which can be co_awaited, used for concurrent execution of strategy fragments and other asynchronous operations
    /**
        This is a coroutine which is managed by quarkbot interface. 
        The function can use co_await. It must use co_return to returns value

        @tparam T type of return value

        To call this function and retrieve the return value you need to use co_await fn(...). You can
        call such function from anothe Async or from StrategyFragment

    */

    template<typename T>
    class Async : public coro::coroutine<T> {
    public:
        class promise_type: public coro::coroutine<T>::promise_type {
        public:            
            void *operator new(std::size_t sz) {return mem_pool.allocate(sz);}
            void operator delete(void *ptr, std::size_t sz) {return mem_pool.deallocate(ptr, sz);}
        };

        Async() = default;
        Async(coro::coroutine<T> x):coro::coroutine<T>(std::move(x)) {}
    };


    ///A fragment of a strategy running concurrently with other fragments
    /**
      StrategyFragment is a special type of Async<void>, which is used to mark strategy fragments.

      The StrategyFragment function() doesn't actually return a value. The type StrategyFragment just
      marks a coroutine. When you call such function, it is immediately started and runs until 
      co_await is reached. If you store StrategyFragment instance into a variable, the function
      is called when the instance is destroyed. You can pass instance into IExecutionWorker::run causing
      function is called in context of the worker - which is recommended in case, that current thread
      is not execution worker
    */
    class StrategyFragment: public Async<void> {
    public:
        using Async<void>::Async;
    };

}