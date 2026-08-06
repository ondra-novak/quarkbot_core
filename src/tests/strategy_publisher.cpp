#include <quarkbot/strategy_publisher.hpp>
#include "basic_coro/coroutine.hpp"
#include "check.h"

#include <string>
#include <thread>
#include <vector>

using quarkbot::EventStream;
using quarkbot::StrategyPublisher;

namespace {

template<typename T>
struct Collector {
    std::vector<T> values = {};
    std::vector<std::size_t> missed = {};
    bool finished = false;
};

///reads the stream until it is closed, records values and count of missed events
template<typename T>
coro::coroutine<void> collect(EventStream<T> stream, Collector<T> &out) {
    T v = {};
    std::size_t m = 0;
    while (co_await stream.receive(v, m)) {
        out.values.push_back(v);
        out.missed.push_back(m);
    }
    out.finished = true;
}

void test_receives_all_published_values() {
    auto pub = StrategyPublisher<int>::create();
    auto sub = pub->subscribe();
    Collector<int> c;
    collect(sub, c);
    for (int i = 1; i <= 5; i++) pub->publish(i);
    pub->close();
    CHECK(c.finished);
    CHECK_EQUAL(c.values.size(), 5u);
    CHECK_EQUAL(c.values.front(), 1);
    CHECK_EQUAL(c.values.back(), 5);
    for (auto m: c.missed) CHECK_EQUAL(m, 0u);
}

void test_reads_values_published_before_receive() {
    auto pub = StrategyPublisher<int>::create(4);
    auto sub = pub->subscribe();
    pub->publish(10);
    pub->publish(20);
    pub->publish(30);
    Collector<int> c;
    collect(sub, c);       //reader starts after the values were published
    pub->close();
    CHECK(c.finished);
    CHECK_EQUAL(c.values.size(), 3u);
    CHECK_EQUAL(c.values[0], 10);
    CHECK_EQUAL(c.values[1], 20);
    CHECK_EQUAL(c.values[2], 30);
}

void test_drops_oldest_events_and_reports_missed() {
    auto pub = StrategyPublisher<int>::create(2);
    auto sub = pub->subscribe();
    for (int i = 1; i <= 5; i++) pub->publish(i);   //nobody is reading, only 4 and 5 survive
    Collector<int> c;
    collect(sub, c);
    pub->close();
    CHECK(c.finished);
    CHECK_EQUAL(c.values.size(), 2u);
    CHECK_EQUAL(c.values[0], 4);
    CHECK_EQUAL(c.values[1], 5);
    CHECK_EQUAL(c.missed[0], 3u);       //1,2,3 were dropped
    CHECK_EQUAL(c.missed[1], 0u);
}

void test_published_value_is_not_moved_away() {
    auto pub = StrategyPublisher<std::string>::create(4);
    auto awaiting = pub->subscribe();
    Collector<std::string> c1;
    collect(awaiting, c1);              //this subscriber waits for the value
    pub->publish(std::string("hello world, a value long enough to not fit in SSO"));
    CHECK_EQUAL(c1.values.size(), 1u);
    CHECK_EQUAL(c1.values[0], "hello world, a value long enough to not fit in SSO");
    //the very same value must be still readable from the queue
    Collector<std::string> c2;
    collect(pub->subscribe(), c2);
    CHECK_EQUAL(c2.values.size(), 1u);
    CHECK_EQUAL(c2.values[0], "hello world, a value long enough to not fit in SSO");
    pub->close();
}

void test_each_subscriber_receives_all_values() {
    auto pub = StrategyPublisher<int>::create(4);
    Collector<int> c1, c2;
    collect(pub->subscribe(), c1);
    collect(pub->subscribe(), c2);
    for (int i = 1; i <= 3; i++) pub->publish(i);
    pub->close();
    CHECK(c1.finished);
    CHECK(c2.finished);
    CHECK_EQUAL(c1.values.size(), 3u);
    CHECK_EQUAL(c2.values.size(), 3u);
    CHECK_EQUAL(c1.values.back(), 3);
    CHECK_EQUAL(c2.values.back(), 3);
}

void test_publish_can_defer_resumption() {
    auto pub = StrategyPublisher<int>::create();
    Collector<int> c;
    collect(pub->subscribe(), c);
    {
        auto ready = pub->publish(42);
        CHECK_EQUAL(ready.size(), 1u);
        CHECK(c.values.empty());        //not resumed yet, resumption is held by 'ready'
    }
    CHECK_EQUAL(c.values.size(), 1u);   //resumed when 'ready' was destroyed
    CHECK_EQUAL(c.values[0], 42);
    pub->close();
}

void test_close_closes_publisher_and_new_receives() {
    auto pub = StrategyPublisher<int>::create();
    Collector<int> c;
    collect(pub->subscribe(), c);
    CHECK(pub->is_open());
    pub->close();
    CHECK(!pub->is_open());
    CHECK(c.finished);
    Collector<int> c2;
    collect(pub->subscribe(), c2);      //subscribing to a closed publisher
    CHECK(c2.finished);                 //must not block, receive() returns false at once
    CHECK(c2.values.empty());
}

void test_publish_after_close_is_ignored() {
    auto pub = StrategyPublisher<int>::create(4);
    pub->publish(1);
    pub->close();
    pub->close();                       //repeated close must be harmless
    auto ready = pub->publish(2);
    CHECK(ready.empty());
    Collector<int> c;
    collect(pub->subscribe(), c);
    CHECK(c.finished);
    CHECK_EQUAL(c.values.size(), 1u);   //what was in the queue is still readable, 2 was not accepted
    CHECK_EQUAL(c.values[0], 1);
}

void test_stream_close_closes_only_that_stream() {
    auto pub = StrategyPublisher<int>::create();
    auto s1 = pub->subscribe();
    Collector<int> c1, c2;
    collect(s1, c1);
    collect(pub->subscribe(), c2);
    pub->publish(1);
    s1.close();
    CHECK(!s1.is_open());
    CHECK(c1.finished);
    CHECK(pub->is_open());
    pub->publish(2);                    //must not touch the closed subscriber
    CHECK_EQUAL(c1.values.size(), 1u);
    CHECK_EQUAL(c2.values.size(), 2u);
    CHECK_EQUAL(c2.values.back(), 2);
    pub->close();
}

void test_receive_on_closed_stream_returns_false() {
    auto pub = StrategyPublisher<int>::create();
    auto s = pub->subscribe();
    s.close();
    pub->publish(1);
    Collector<int> c;
    collect(s, c);
    CHECK(c.finished);
    CHECK(c.values.empty());
    pub->close();
}

void test_current_returns_last_value() {
    auto pub = StrategyPublisher<int>::create();
    auto s = pub->subscribe();
    int v = -1;
    CHECK(!s.current(v));               //nothing published yet
    pub->publish(7);
    CHECK(s.current(v));
    CHECK_EQUAL(v, 7);
    v = -1;
    CHECK(s.current(v));                //repeated call returns the same value
    CHECK_EQUAL(v, 7);
    pub->close();
}

void test_concurrent_publish_and_receive() {
    static constexpr int count = 20000;
    auto pub = StrategyPublisher<int>::create(16);
    auto s = pub->subscribe();

    std::thread producer([&]{
        for (int i = 0; i < count; i++) pub->publish(i);
        pub->close();
    });

    int received = 0;
    int last = -1;
    std::size_t total_missed = 0;
    int v = 0;
    std::size_t m = 0;
    while (s.receive(v, m).get()) {
        CHECK_GREATER(v, last);         //values are always delivered in order
        last = v;
        total_missed += m;
        ++received;
    }
    producer.join();
    CHECK_GREATER(received, 0);
    CHECK_EQUAL(static_cast<std::size_t>(received) + total_missed, static_cast<std::size_t>(last) + 1);
}

}

int main() {
    test_receives_all_published_values();
    test_reads_values_published_before_receive();
    test_drops_oldest_events_and_reports_missed();
    test_published_value_is_not_moved_away();
    test_each_subscriber_receives_all_values();
    test_publish_can_defer_resumption();
    test_close_closes_publisher_and_new_receives();
    test_publish_after_close_is_ignored();
    test_stream_close_closes_only_that_stream();
    test_receive_on_closed_stream_returns_false();
    test_current_returns_last_value();
    test_concurrent_publish_and_receive();
    return 0;
}
