#include "quarkbot/strategy_fragment.hpp"
#include "basic_coro/coroutine.hpp"
#include "basic_coro/sync_await.hpp"
#include "check.h"
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


int main() {
    test1();
    return 0;
}