#pragma once

#include "basic_coro/awaitable.hpp"
#include "basic_coro/prepared_coro.hpp"
#include <atomic>
#include <coroutine>
namespace quarkbot {


///counts open scopes, allows to wait on clean (when count is zero)
/**
Works as lock. Use std::scope_lock to track count of scopes
 */
class ScopeCounter {
public:
    ///enter scope
    void lock() {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
    ///exit scope
    void unlock() {
        if (counter.fetch_sub(1, std::memory_order_relaxed) == 1) {
            counter.notify_all();
        }
        
    }

    ///try to join, non-blocking
    /**
    @retval true joined
    @retval false would block
     */
    bool try_join() {
        return counter.load(std::memory_order_relaxed) == 0;
    }
    ///wait until all scopes are exited
    void join() {
        auto n = counter.load(std::memory_order_relaxed);
        while (n) {
            counter.wait(n);
             n = counter.load(std::memory_order_relaxed);
        }
    }

protected:
    std::atomic<int> counter;
};


///count scopes, allows co_await
class ScopeCounterAsync : public ScopeCounter{ 
public:

    void unlock() {
        if (counter.fetch_sub(1, std::memory_order_relaxed) == 1) {
            counter.notify_all();
            auto x = std::coroutine_handle<>::from_address(coro_handle.exchange(nullptr, std::memory_order_relaxed));
            if (x) x.resume();
        }
    }

    coro::awaitable<void> operator co_await() {
        //always suspend
        return [this](auto promise) -> coro::prepared_coro{            
            //pre-resolve promise, we will work with handle
            coro::prepared_coro p = promise();
            void *need = nullptr;
            void *new_val = p.release().address();
            //attempt to put coro handle atomically
            if (coro_handle.compare_exchange_strong(need, new_val)) {
                //but collision - go on
                return {std::coroutine_handle<>::from_address(new_val)};
            }
            if (counter.load(std::memory_order_relaxed) == 0) {
                auto *a = coro_handle.exchange(nullptr, std::memory_order_relaxed);
                if (a){
                   return {std::coroutine_handle<>::from_address(a)};
                } 
                return {};
            }
            return {};                            
        };
    }

protected:
    std::atomic<void *> coro_handle;
};


}