#include "ifc/hub.hpp"
#include "../ifc/strategy_publisher.hpp"
#include "ifc/strategy_fragment.hpp"
#include "ifc/streaming.hpp"
#include "check.h"


quarkbot::StrategyFragment print_events(quarkbot::PHub<int>  s) {
    int v;
    bool r;
    for (int i = 0; i < 5; i++) {
        bool r = co_await s->receive(v);
        CHECK(r);
        CHECK_EQUAL(v, i);
    }
    r = co_await s->receive(v);
    CHECK(!r);
    co_return;
}

quarkbot::StrategyFragment send_events(quarkbot::PHub<int>  s) {
    for (int i = 0; i < 5; i++) {
        co_await s->send(i);
    }
    s->close();
}
    
quarkbot::StrategyFragment print_events_2(quarkbot::PHub<int>  s) {
    std::optional<int> v;
    bool r;
    for (int i = 0; i < 5; i++) {
        bool r = co_await s->receive(v);
        CHECK(r);
        CHECK_EQUAL(*v, i);
    }
    r = co_await s->receive(v);
    CHECK(!r);
    co_return;
}

quarkbot::StrategyFragment send_events_2(quarkbot::PHub<int>  s) {
    for (int i = 0; i < 5; i++) {
        co_await s->send(std::move(i));
    }
    s->close();
}


void test1() {
    auto h = quarkbot::Hub<int>::create();
    print_events(h);
    send_events(h);
}
void test2() {
    auto h = quarkbot::Hub<int>::create();
    send_events(h);
    print_events(h);
}
void test3() {
    auto h = quarkbot::Hub<int>::create();
    print_events_2(h);
    send_events_2(h);
}
void test4() {
    auto h = quarkbot::Hub<int>::create();
    send_events_2(h);
    print_events_2(h);
}


int main() {
    test1();
    test2();
    test3();
    test4();
    return 0;
}