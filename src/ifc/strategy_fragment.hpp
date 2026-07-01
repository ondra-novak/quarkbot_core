#pragma once
#include "basic_coro/coroutine.hpp"
#include "basic_coro/exceptions.hpp"
#include "basic_coro/pending.hpp"
#include "ifc/execution_worker.hpp"
#include "ifc/memory.hpp"
#include <coroutine>
#include <list>
#include <mutex>
#include <source_location>
namespace quarkbot {


    ///Asynchronous function which can be co_awaited, used for concurrent execution of strategy fragments and other asynchronous operations
    /**
        This is a coroutine which is managed by quarkbot interface. 
        The function can use co_await. It must use co_return to returns value

        @tparam T type of return value

        To call this function and retrieve the return value you need to use co_await fn(...). You can
        call such function from anothe Async or from StrategyFragment

    */

    class IAsyncDebugTrace {
    public:

        virtual void add(std::coroutine_handle<> h, const std::source_location &loc) = 0;
        virtual void remove(std::coroutine_handle<> h) = 0;
        virtual void resumed(std::coroutine_handle<> h) = 0;        
        virtual ~IAsyncDebugTrace() = default;
        static IAsyncDebugTrace *trace;
    };

    inline IAsyncDebugTrace * IAsyncDebugTrace::trace = nullptr;


    template<typename T>
    class Async : public coro::coroutine<T> {
    public:

    class promise_type: public coro::coroutine<T>::promise_type {
        public:            
            void *operator new(std::size_t sz) {return LockFreeFramePool::allocate(sz);}
            void operator delete(void *ptr, std::size_t sz) {return LockFreeFramePool::deallocate(ptr, sz);}
            
            
            void (*old_resume)(std::coroutine_handle<promise_type>) = nullptr;

            static void debug_traced_resume(std::coroutine_handle<promise_type> h) {
                IAsyncDebugTrace::trace->resumed(h);
                auto &me = h.promise();
                me.old_resume(h);
            }

            std::suspend_always initial_suspend(std::source_location loc = std::source_location::current())  noexcept {
                if (IAsyncDebugTrace::trace) {
                    auto h = std::coroutine_handle<promise_type>::from_promise(*this);
                    IAsyncDebugTrace::trace->add(h, loc);                
                    auto frame_ptr = reinterpret_cast<void (**)(std::coroutine_handle<promise_type>) >(h.address());
                    old_resume = *frame_ptr;
                    *frame_ptr = &debug_traced_resume;
                }
                return {};
            }        

            ~promise_type() {
                if (IAsyncDebugTrace::trace) IAsyncDebugTrace::trace->remove(std::coroutine_handle<promise_type>::from_promise(*this));
            }
            
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
        struct promise_type : Async<void>::promise_type {
            void unhandled_exception() {
                //unhandled exception in strategy fragment cannot be received on target awaiter
                //so report that fragment is done
                this->return_void();
                //report exception
                coro::async_unhandled_exception();
            }
        };
    };

    ///Creates execution group of multiple strategy fragments
    /**
        Join is awaitable, so you can co_await on whole group
        The group have own garbage collector, which allows to remove finished
        fragments
    */
    class StrategyFragmentGroup {
    public:

        ///Add fragment o group, the fragment is started, and becomes part of the group
        void add(StrategyFragment frag) {
            std::lock_guard _(_mx);
            add(std::move(frag), ExecutionWorker::current().required());
        }


        ///Add fragment o group, the fragment is started, and becomes part of the group
        void add(StrategyFragment frag, ExecutionWorker &worker) {
            std::lock_guard _(_mx);
            if (--next_gc == 0) {
                for (auto iter = _pending_list.begin(); iter != _pending_list.end();) {
                    if (iter->await_ready()) {
                        iter->await_resume();
                        iter = _pending_list.erase(iter);
                    } else {
                        ++iter;
                    }
                }
            }
            _pending_list.emplace_front(std::move(frag), [&](auto p){
                worker.resume(std::move(p));
            });
            next_gc = _pending_list.size();
        }

        ///join the group (synchronize with group completion)
        awaitable<void> join() {
            std::lock_guard _(_mx);
            auto waiter = [](std::list<coro::pending<StrategyFragment> > list) mutable -> StrategyFragment {
                for (auto &x: list) {
                    co_await x;
                }
            };
            return waiter(std::move(_pending_list));
        }

    protected:
        std::mutex _mx;
         std::list<coro::pending<StrategyFragment> >  _pending_list;
        std::size_t next_gc = 1;

    };

    

}