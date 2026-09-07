#include "tests/check.h"
#include "../quarkbot/common/thread_executor.hpp"

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace quarkbot;

namespace {

///Burn a few cycles without letting the compiler fold the loop away.
void spin(int n) {
    for (int i = 0; i < n; ++i) std::atomic_signal_fence(std::memory_order_seq_cst);
}

///Run `fn` on a helper thread and abort the process if it does not finish in time.
/**
 * A lost wakeup in the worker loop makes ~ThreadExecutor block in join() forever.
 * There is no way to unwedge that thread, so the watchdog reports the failure and
 * terminates hard instead of letting ctest sit on a stuck process.
 */
template<typename Fn>
void with_watchdog(const char *what, std::chrono::seconds limit, Fn &&fn) {
    std::atomic<bool> done(false);
    std::thread worker([&]{
        fn();
        done.store(true, std::memory_order_release);
    });
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::cerr << "FAILED: " << what << " did not finish in "
                      << limit.count() << "s - stuck in ~ThreadExecutor"
                      << REPORT_LOCATION << std::endl;
            std::cerr.flush();
            std::_Exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    worker.join();
    std::cout << "Passed: " << what << std::endl;
}

///Destroy the executor right after creation, from a foreign thread.
/**
 * Targets the window between the worker's stop_requested() check and the
 * untimed _cv.wait(): the stop notification must not be lost there.
 */
void test_destroy_races_with_worker_startup() {
    with_watchdog("destroy right after create", std::chrono::seconds(20), []{
        for (int i = 0; i < 20000; ++i) {
            auto ex = ThreadExecutor::create();
            //sweep the delay so the stop request lands at varying points of the startup path
            spin(i % 64);
            ex.reset();
        }
    });
}

///Destroy the executor after it has parked in the untimed wait.
void test_destroy_idle_executor() {
    with_watchdog("destroy idle executor", std::chrono::seconds(20), []{
        for (int i = 0; i < 200; ++i) {
            auto ex = ThreadExecutor::create();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ex.reset();
        }
    });
}

///Destroy the executor while its worker is cycling through resumed tasks.
void test_destroy_busy_executor() {
    with_watchdog("destroy busy executor", std::chrono::seconds(20), []{
        for (int i = 0; i < 2000; ++i) {
            auto ex = ThreadExecutor::create();
            for (int j = 0; j < 8; ++j) ex->resume(std::noop_coroutine());
            //sweep the delay so the stop request lands at varying points of the loop
            spin(i % 128);
            ex.reset();
        }
    });
}

///A coroutine that stays suspended until the worker resumes it, then blocks the
///worker thread inside the task long enough for the owner to drop its reference.
struct BlockingTask {
    struct promise_type {
        BlockingTask get_return_object() {
            return BlockingTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() {return {};}
        std::suspend_never final_suspend() noexcept {return {};}
        void return_void() {}
        void unhandled_exception() {}
    };
    std::coroutine_handle<> h;
};

BlockingTask blocking_task(std::atomic<bool> *started) {
    started->store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    co_return;
}

///Let the worker thread run ~ThreadExecutor on itself (the _thr.detach() branch).
/**
 * The worker is the last owner here, so it destroys the executor from inside its own
 * task. The stop callback then takes _mx on this very thread - it must not deadlock.
 */
void test_worker_destroys_itself() {
    with_watchdog("worker destroys itself", std::chrono::seconds(20), []{
        for (int i = 0; i < 30; ++i) {
            auto ex = ThreadExecutor::create();
            std::atomic<bool> started(false);
            auto t = blocking_task(&started);
            ex->resume(t.h);
            while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
            //the worker is inside the task with _mx unlocked and holds an extra reference,
            //so dropping ours leaves it as the last owner
            ex.reset();
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    });
}

}

int main() {
    test_destroy_races_with_worker_startup();
    test_destroy_idle_executor();
    test_destroy_busy_executor();
    test_worker_destroys_itself();
    return 0;
}
