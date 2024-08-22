#include "../common/dispatcher_thread.h"
#include "check.h"

template class DispatcherCore<int>;

void test1() {
    DispatcherCore<int> q;
    std::array numbers = {10,8,2,37,6,8,2,8,89,8};
    for (int i: numbers) q.post(i);
    auto iter = numbers.begin();
    while (q.process_message([&](int x){
        CHECK_EQUAL(*iter, x);
        ++iter;
        return true;
    }));
    CHECK(iter == numbers.end());

}

void test2() {
    DispatcherCore<int> q;
    std::array numbers = {10,8,2,37,6,8,2,8,89,8};
    std::array res_numbers = {10,37,6,2,89,8};
    for (int i: numbers) q.post_collapse(i);
    auto iter = res_numbers.begin();
    while (q.process_message([&](int x){
        CHECK_EQUAL(*iter, x);
        ++iter;
        return true;
    }));
    CHECK(iter == res_numbers.end());

}

void test3() {
    DispatcherCore<int> q;
    auto now = std::chrono::system_clock::now();
    std::array numbers = {10,8,2,37,6,8,2,8,89,8};
    for (int i: numbers) q.post_timed(now+std::chrono::seconds(i),i);
    std::sort(numbers.begin(), numbers.end());
    auto iter = numbers.begin();
    while (q.process_message(now+std::chrono::hours(1),[&](int x){
        CHECK_EQUAL(*iter, x);
        ++iter;
        return true;
    }));
    CHECK(iter == numbers.end());

}

int main() {
    DispatcherCore<int> q;
    test1();
    test2();
    test3();

}
