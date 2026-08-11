#include "quarkbot/strategy_fragment.hpp"
#include "basic_coro/coroutine.hpp"
#include "basic_coro/sync_await.hpp"
#include "check.h"
#include "quarkbot/backtest/backtest_executor.hpp"
#include "quarkbot/common/thread_executor.hpp"
#include "quarkbot/execution_worker.hpp"
#include <chrono>
#include <thread>



using namespace quarkbot;

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



int main() {
    test1();
    test2();
    test3();
    return 0;
}