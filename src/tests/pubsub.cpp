#include "../ifc/strategy_publisher.hpp"
#include "ifc/strategy_fragment.hpp"
#include "check.h"


quarkbot::StrategyFragment print_events(quarkbot::EventStream<int> s) {
    int v;
    bool r;
    for (int i = 0; i < 5; i++) {
        bool r = co_await s.receive(v);
        CHECK(r);
        CHECK_EQUAL(v, i);
    }
    r = co_await s.receive(v);
    CHECK(!r);
    co_return;
}
    

void test1() {
    auto pub = quarkbot::StrategyPublisher<int>::create();
    auto sub = pub->subscribe();
    auto sub_ptr = sub.get();
    print_events(std::move(sub));
    for (int i = 0; i < 5; i++) {
        pub->publish(i);
    }
    //the coroutine is still awaiting so pointer is valid
    sub_ptr->close();
}


int main() {
    test1();
    return 0;
}