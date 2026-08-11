#include "quarkbot/strategy_fragment.hpp"
#include "basic_coro/coroutine.hpp"
#include "basic_coro/sync_await.hpp"
#include "check.h"
#include "quarkbot/backtest/backtest_executor.hpp"
#include "quarkbot/common/thread_executor.hpp"
#include "quarkbot/execution_worker.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>



using namespace quarkbot;

///Kills the process if the guarded scope doesn't finish in time
/**
    Defects in quiesce() show up as a hang, not as a wrong value, and ctest registers
    no per-test timeout - without this, a regression would block the whole test suite
    instead of failing this test.
*/
class Watchdog {
public:
    Watchdog(std::chrono::milliseconds limit, const char *what):_what(what) {
        _thr = std::thread([=,this]{
            std::unique_lock lk(_mx);
            if (!_cv.wait_for(lk, limit, [&]{return _done;})) {
                std::cerr << "FAILED: timeout after " << limit.count() << " ms (" << _what << ")"
                          << std::endl;
                std::_Exit(1);
            }
        });
    }
    Watchdog(const Watchdog &) = delete;
    Watchdog &operator=(const Watchdog &) = delete;
    ~Watchdog() {
        {
            std::scoped_lock _(_mx);
            _done = true;
        }
        _cv.notify_all();
        _thr.join();
    }
protected:
    const char *_what;
    std::mutex _mx;
    std::condition_variable _cv;
    std::thread _thr;
    bool _done = false;
};

///How long a single task deliberately occupies the worker thread
static constexpr auto task_work_time = std::chrono::milliseconds(200);

coro::coroutine<void> resume_in_different_thread(ExecutionWorker thr2) {
    co_await thr2.schedule();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}


StrategyFragment test_frag(ExecutionWorker thr2) {
    auto id1 = std::this_thread::get_id();
    co_await resume_in_different_thread(thr2);
    auto id2 = std::this_thread::get_id();
    CHECK(id1 == id2);
    co_return;
}

void test1() {
    ExecutionWorker thr1( ThreadExecutor::create());
    ExecutionWorker thr2 ( ThreadExecutor::create());

    coro::sync_await(thr1.launch(test_frag(thr2)));

}

StrategyFragment main_cycle(int &counter) {
    for (int i = 0; i < 10;++i) {
        co_await ExecutionWorker::current().schedule();
        ++counter;
    }
}



StrategyFragment idle_worker(int &counter, bool &called) {
    co_await ExecutionWorker::current().sleep_until_idle();
    CHECK_EQUAL(counter, 10);    
    ExecutionWorker::current().run(main_cycle(counter));
    co_await ExecutionWorker::current().sleep_until_idle();
    CHECK_EQUAL(counter, 20);    
    called = true;
}

void test2() {

    int counter = 0;
    bool called = false;
    ThreadExecutor::attach([&](std::shared_ptr<ThreadExecutor> ptr){
        ExecutionWorker wrk(ptr);
        wrk.run(idle_worker(counter, called));
        wrk.run(main_cycle(counter));
    });
    CHECK(called);


}

void test3() {

    auto exc = BacktestExecutor::create();

    int counter = 0;
    bool called = false;
    ExecutionWorker wrk(exc);

    wrk.run(idle_worker(counter, called));
    wrk.run(main_cycle(counter));

    exc->flush_queue();

    CHECK(called);


}


StrategyFragment counting_task(std::atomic<int> &counter) {
    counter.fetch_add(1);
    co_return;
}

StrategyFragment blocking_idle_task(std::atomic<bool> &started, std::atomic<int> &done) {
    co_await ExecutionWorker::current().sleep_until_idle();
    started.store(true);
    std::this_thread::sleep_for(task_work_time);    //deliberately occupy the worker
    done.fetch_add(1);
}

///Arms all idle tasks from inside the worker, so that arming cannot interleave with the idle drain
StrategyFragment arm_idle_tasks(std::atomic<bool> &started, std::atomic<int> &done, int count) {
    for (int i = 0; i < count; ++i) {
        ExecutionWorker::current().run(blocking_idle_task(started, done));
    }
    co_return;
}

///quiesce() must not be satisfied by finished idle tasks
/**
    The worker counts finished tasks in a single counter, but quiesce() derives its
    target from the main queue only. If idle completions were counted as well,
    quiesce() would return before the main-queue tasks it was asked to wait for ran.
*/
void test4() {
    Watchdog wd(std::chrono::seconds(10), "test4: quiesce() vs idle queue");
    ExecutionWorker wrk(ThreadExecutor::create());
    std::atomic<bool> idle_started = {false};
    std::atomic<int> idle_done = {0};
    std::atomic<int> dispatch_done = {0};
    constexpr int idle_count = 3;

    wrk.run(arm_idle_tasks(idle_started, idle_done, idle_count));
    while (!idle_started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    //the worker is now busy inside the idle drain for about idle_count*task_work_time
    wrk.run(counting_task(dispatch_done));
    wrk.run(counting_task(dispatch_done));
    CHECK_EQUAL(dispatch_done.load(), 0);
    wrk.quiesce();
    CHECK_EQUAL(dispatch_done.load(), 2);

    //let the remaining idle tasks finish before the executor loses its last reference
    while (idle_done.load() < idle_count) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

StrategyFragment quiesce_on_worker(std::atomic<int> &done, std::atomic<bool> &returned) {
    ExecutionWorker::current().run(counting_task(done));
    ExecutionWorker::current().quiesce();
    CHECK_EQUAL(done.load(), 1);
    returned.store(true);
    co_return;
}

///quiesce() called from the executor's own thread must flush the queue instead of waiting for it
/**
    The worker thread has to be recognized even when it was attached rather than spawned -
    attach() never fills _thr, so an identity check based on _thr would send the worker
    into the cross-thread branch and make it wait for a counter only it can advance.
*/
void test5() {
    Watchdog wd(std::chrono::seconds(10), "test5: quiesce() on an attached thread");
    std::atomic<bool> returned = {false};
    std::atomic<int> done = {0};
    ThreadExecutor::attach([&](std::shared_ptr<ThreadExecutor> ptr){
        ExecutionWorker wrk(ptr);
        wrk.run(quiesce_on_worker(done, returned));
    });
    CHECK(returned.load());
}

StrategyFragment blocking_task(std::atomic<bool> &started) {
    started.store(true);
    std::this_thread::sleep_for(task_work_time);     //deliberately occupy the worker
    co_return;
}

StrategyFragment slow_counting_task(std::atomic<int> &counter) {
    std::this_thread::sleep_for(task_work_time);     //deliberately occupy the worker
    counter.fetch_add(1);
    co_return;
}

///quiesce() must wait for the running task AND for everything queued behind it
/**
    The queued tasks have to be slow on purpose. With trivial tasks the worker finishes
    them inside the window between waking the waiting thread up and that thread
    re-reading the counter, so the test would pass even when quiesce() stopped waiting
    one task too early.
*/
void test6() {
    Watchdog wd(std::chrono::seconds(10), "test6: quiesce() covers the whole main queue");
    ExecutionWorker wrk(ThreadExecutor::create());
    std::atomic<bool> started = {false};
    std::atomic<int> done = {0};

    wrk.run(blocking_task(started));
    while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    wrk.run(slow_counting_task(done));
    wrk.run(slow_counting_task(done));
    CHECK_EQUAL(done.load(), 0);
    wrk.quiesce();
    CHECK_EQUAL(done.load(), 2);
}


int main() {
    test1();
    test2();
    test3();
    test4();
    test5();
    test6();
    return 0;
}