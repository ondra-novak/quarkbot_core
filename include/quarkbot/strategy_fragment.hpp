#pragma once
#include "async.hpp"
#include "basic_coro/awaitable.hpp"
#include "quarkbot/log.hpp"
#include <list>

namespace quarkbot {

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
                logOutputCB(LogLevel::error, 
                     [&]{return std::pair(coro_location,"Strategy fragment unhandled exception");});
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
        void run(StrategyFragment frag) {            
            run(std::move(frag), ExecutionWorker::current().required());
        }


        ///Add fragment o group, the fragment is started, and becomes part of the group
        void run(StrategyFragment frag, ExecutionWorker &worker) {
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
            auto waiter = [](std::list<coro::pending<StrategyFragment> > list) mutable -> coro::awaitable<void> {
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