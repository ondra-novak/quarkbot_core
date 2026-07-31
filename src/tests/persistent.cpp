#include "quarkbot/persistent.hpp"
#include "basic_coro/sync_await.hpp"
#include "check.h"
#include "quarkbot/common/mem_storage.hpp"
#include "quarkbot/common/thread_executor.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/strategy_fragment.hpp"



quarkbot::StrategyFragment store_test_1(quarkbot::Storage storage) {
    quarkbot::Persistent<int> val(storage, "test",42);
    quarkbot::Persistent<std::string> strval(storage, "test_str");

    CHECK_EQUAL(static_cast<int>(val), 42);

    val.set(12);
    CHECK_EQUAL(static_cast<int>(val), 12);
    CHECK_EQUAL(static_cast<std::string>(strval),"");
    strval.set("hello world");
    CHECK_EQUAL(static_cast<std::string>(strval),"hello world");
    co_return;
}
quarkbot::StrategyFragment store_test_1_followup(quarkbot::Storage storage) {
    quarkbot::Persistent<int> val2(storage, "test",42);
    CHECK_EQUAL(static_cast<int>(val2), 12);
    quarkbot::Persistent<std::string> strval(storage, "test_str");
    CHECK_EQUAL(static_cast<std::string>(strval),"hello world");
    co_return;    
}

quarkbot::StrategyFragment store_test_2(quarkbot::Storage storage) {
    quarkbot::PersistentNamespace ns1(storage, "ns1");
    quarkbot::PersistentNamespace ns2(storage, "ns2");
    quarkbot::Persistent<int> val(ns1, "test",42);
    quarkbot::Persistent<std::string> strval(ns2, "test");

    CHECK_EQUAL(static_cast<int>(val), 42);

    val.set(12);
    CHECK_EQUAL(static_cast<int>(val), 12);
    CHECK_EQUAL(static_cast<std::string>(strval),"");
    strval.set("hello world");
    CHECK_EQUAL(static_cast<std::string>(strval),"hello world");
    co_return;
}
quarkbot::StrategyFragment store_test_2_followup(quarkbot::Storage storage) {
    quarkbot::PersistentNamespace ns1(storage, "ns1");
    quarkbot::PersistentNamespace ns2(storage, "ns2");
    quarkbot::Persistent<int> val(ns1, "test",42);
    quarkbot::Persistent<std::string> strval(ns2, "test");
    CHECK_EQUAL(static_cast<int>(val), 12);
    CHECK_EQUAL(static_cast<std::string>(strval),"hello world");
    co_return;    
}


void test1() {
    quarkbot::ExecutionWorker worker ( quarkbot::ThreadExecutor::create());
    quarkbot::Storage storage (quarkbot::MemStorage::create());
    coro::sync_await(worker.launch(store_test_1(storage)));
    coro::sync_await(worker.launch(store_test_1_followup(storage)));


}

void test2() {
    quarkbot::ExecutionWorker worker ( quarkbot::ThreadExecutor::create());
    quarkbot::Storage storage (quarkbot::MemStorage::create());
    coro::sync_await(worker.launch(store_test_2(storage)));
    coro::sync_await(worker.launch(store_test_2_followup(storage)));
}


int main() {
    test1();
    test2();
}
