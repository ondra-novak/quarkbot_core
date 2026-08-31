#include "../quarkbot/streaming/queue_as_stream.hpp"
#include "../quarkbot/streaming/iterator_as_stream.hpp"
#include "basic_coro/sync_await.hpp"
#include "check.h"
#include "quarkbot/backtest/backtest_executor.hpp"
#include "quarkbot/common/thread_executor.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/execution_worker.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>


static int checkpoint_counter = 0;

using namespace quarkbot;

///Kills the process if the guarded scope doesn't finish in time
/**
    A lost event or a missed resumption shows up as a coroutine which never finishes, and ctest
    registers no per-test timeout - without this a regression would block the whole suite instead
    of failing this test.

    @note duplicated from strategy_fragment.cpp - worth hoisting into check.h once a third test
        needs it
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


// ---------------------------------------------------------------------------
// basic single threaded handshake between publisher and receiver
// ---------------------------------------------------------------------------

StrategyFragment test_receive(EventStream<int> stream) {

    int val;
    bool b = co_await stream.receive(val);
    CHECK(b);
    CHECK(val == 42);
    checkpoint_counter++;
    b = co_await stream.receive(val);
    CHECK(b);
    CHECK(val == 56);
    checkpoint_counter++;
    b = co_await stream.receive(val);
    CHECK(b);
    CHECK(val == 77);
    checkpoint_counter++;
    b = stream.current(val);
    CHECK(b);
    CHECK(val == 101);
    checkpoint_counter++;
    b = stream.current(val);
    CHECK(!b);//not awaitable now
    CHECK(stream.is_open());
    b = co_await stream.receive(val);
    checkpoint_counter++;
    CHECK(!b);
    CHECK(!stream.is_open());
}

StrategyFragment test_publish(std::shared_ptr<QueueAsStream<int> > stream) {
    ExecutionWorker worker = ExecutionWorker::current();
    CHECK_EQUAL(checkpoint_counter, 0);
    stream->publish(42);
    co_await worker.schedule();
    stream->publish(56);
    stream->publish(77);
    stream->publish(101);
    co_await worker.schedule();
    CHECK_EQUAL(checkpoint_counter, 4);
    stream->close();
    co_await worker.schedule();
    CHECK_EQUAL(checkpoint_counter, 5);
}

void test_basic(ExecutionWorker ewrk, std::shared_ptr<BacktestExecutor> wrk) {
    auto q = std::make_shared<QueueAsStream<int>>();
    auto p1 = ewrk.launch(test_receive(EventStream<int>(q)));
    auto p2 = ewrk.launch(test_publish(std::move(q)));
    wrk->flush_queue();
    CHECK(p1.await_ready());
    CHECK(p2.await_ready());
}

// ---------------------------------------------------------------------------
// closing must not throw away what was already published
// ---------------------------------------------------------------------------

StrategyFragment drain_all(EventStream<int> stream, int &count, int &sum) {
    int val;
    while (co_await stream.receive(val)) {
        ++count;
        sum += val;
    }
}

///the publisher's normal end of transfer: publish the series, then close
/**
    This is the invariant the whole class exists for - nothing published may be lost, not even
    when the source finishes (and closes) long before the strategy gets to read.
*/
void test_drain_after_close(ExecutionWorker ewrk, std::shared_ptr<BacktestExecutor> wrk) {
    auto q = std::make_shared<QueueAsStream<int>>();
    for (int i = 1; i <= 3; ++i) CHECK(q->publish(i).has_value());
    q->close();

    //closed for the publisher, still readable for the receiver - the two predicates disagree
    CHECK(!q->can_publish());
    CHECK(q->is_open());
    CHECK(!q->publish(4).has_value());

    int count = 0, sum = 0;
    auto p = ewrk.launch(drain_all(EventStream<int>(q), count, sum));
    wrk->flush_queue();
    CHECK(p.await_ready());
    CHECK_EQUAL(count, 3);
    CHECK_EQUAL(sum, 6);
    //drained now, so both sides agree
    CHECK(!q->is_open());
    CHECK(!q->can_publish());
}

///a long series published with nobody reading must arrive complete and in order
void test_fifo_order(ExecutionWorker ewrk, std::shared_ptr<BacktestExecutor> wrk) {
    constexpr int cnt = 1000;
    auto q = std::make_shared<QueueAsStream<int>>();
    for (int i = 0; i < cnt; ++i) q->publish(i);
    q->close();

    int expected = 0;
    bool order_ok = true;
    auto reader = [](std::shared_ptr<QueueAsStream<int> > q, int &expected,
                     bool &order_ok) -> StrategyFragment {
        EventStream<int> stream(q);
        int val;
        while (co_await stream.receive(val)) {
            if (val != expected) order_ok = false;
            ++expected;
        }
    };
    auto p = ewrk.launch(reader(q, expected, order_ok));
    wrk->flush_queue();
    CHECK(p.await_ready());
    CHECK(order_ok);
    CHECK_EQUAL(expected, cnt);
}

// ---------------------------------------------------------------------------
// stream which is dead before it is used
// ---------------------------------------------------------------------------

void test_born_closed() {
    QueueAsStream<int> q(true);
    CHECK(!q.is_open());
    CHECK(!q.can_publish());
    CHECK(!q.publish(1).has_value());
    int val = -1;
    CHECK(!q.current(val));
    CHECK(!coro::sync_await(q.receive(val)));   //resolved immediately, does not suspend
}

// ---------------------------------------------------------------------------
// close() as the receiver's escape hatch (timeout pattern)
// ---------------------------------------------------------------------------

StrategyFragment wait_forever(EventStream<int> stream, bool &resumed) {
    int val;
    bool b = co_await stream.receive(val);
    CHECK(!b);
    resumed = true;
    //stream (and its shared_ptr) is released when this frame is destroyed
}

///the receiver blocked in receive() must be released by close() and must then drop the instance
/**
    This is what close() is actually for: the publisher holds only a weak_ptr, so the transfer ends
    by the receiver's instance dying - but a coroutine suspended in receive() holds that instance
    forever. close() resumes it with false so the frame can finish and let go.
*/
void test_close_releases_receiver(ExecutionWorker ewrk, std::shared_ptr<BacktestExecutor> wrk) {
    Watchdog wd(std::chrono::seconds(10), "close() must resume the awaiting receiver");
    bool resumed = false;
    std::weak_ptr<QueueAsStream<int> > weak;
    {
        auto q = std::make_shared<QueueAsStream<int>>();
        weak = q;
        auto p = ewrk.launch(wait_forever(EventStream<int>(q), resumed));
        wrk->flush_queue();
        CHECK(!resumed);                //suspended inside receive()
        CHECK(!weak.expired());
        q->close();                     //e.g. fired by a timeout
        wrk->flush_queue();
        CHECK(p.await_ready());
        CHECK(resumed);
    }
    //publisher's weak_ptr now reports the transfer is over
    CHECK(weak.expired());
}

///dropping the receiver's instance is the primary way to end the transfer - no close() needed
void test_receiver_drop_expires_weak_ptr(ExecutionWorker ewrk,
                                         std::shared_ptr<BacktestExecutor> wrk) {
    std::weak_ptr<QueueAsStream<int> > weak;
    int count = 0, sum = 0;
    {
        auto q = std::make_shared<QueueAsStream<int>>();
        weak = q;
        q->publish(7);
        q->close();
        auto p = ewrk.launch(drain_all(EventStream<int>(q), count, sum));
        wrk->flush_queue();
        CHECK(p.await_ready());
        CHECK(!weak.expired());         //q still alive here
    }
    CHECK_EQUAL(count, 1);
    CHECK(weak.expired());
}

// ---------------------------------------------------------------------------
// stop token path of EventStreamStoppable
// ---------------------------------------------------------------------------

void test_stop_token(ExecutionWorker ewrk, std::shared_ptr<BacktestExecutor> wrk) {
    Watchdog wd(std::chrono::seconds(10), "stop token must close the stream");
    auto q = std::make_shared<QueueAsStream<int>>();
    std::stop_source stop;
    bool resumed = false;
    auto p = ewrk.launch(wait_forever(EventStream<int>(q).stop_on(stop), resumed));
    wrk->flush_queue();
    CHECK(!resumed);
    stop.request_stop();
    CHECK(!q->can_publish());           //stop callback already ran close()
    wrk->flush_queue();
    CHECK(p.await_ready());
    CHECK(resumed);
}

// ---------------------------------------------------------------------------
// move only payload
// ---------------------------------------------------------------------------

using UPtr = std::unique_ptr<int>;

StrategyFragment receive_uptr(std::shared_ptr<QueueAsStream<UPtr> > q, int &sum) {
    UPtr val;
    while (co_await q->receive(val)) sum += *val;
}

///the payload must not have to be copyable - a downloaded batch is typically move only
void test_move_only(ExecutionWorker ewrk, std::shared_ptr<BacktestExecutor> wrk) {
    auto q = std::make_shared<QueueAsStream<UPtr>>();
    q->publish(std::make_unique<int>(10));
    q->publish(std::make_unique<int>(20));
    q->close();
    int sum = 0;
    auto p = ewrk.launch(receive_uptr(q, sum));
    wrk->flush_queue();
    CHECK(p.await_ready());
    CHECK_EQUAL(sum, 30);
}

// ---------------------------------------------------------------------------
// the real use case: publisher on a foreign thread, receiver on its own worker
// ---------------------------------------------------------------------------

StrategyFragment mt_consumer(std::shared_ptr<QueueAsStream<int> > q, std::atomic<int> &count,
                             std::atomic<long> &sum, std::atomic<bool> &order_ok,
                             std::atomic<bool> &single_thread, std::thread::id producer_thread) {
    auto my_thread = std::this_thread::get_id();
    int val;
    int expected = 0;
    while (co_await q->receive(val)) {
        //the whole point of the design: publishing must never run this code on the source's thread
        if (std::this_thread::get_id() != my_thread) single_thread.store(false);
        if (std::this_thread::get_id() == producer_thread) single_thread.store(false);
        if (val != expected) order_ok.store(false);
        ++expected;
        sum.fetch_add(val);
        count.fetch_add(1);
    }
}

///a publisher running on a foreign thread must lose nothing and must not execute strategy code
void test_multithreaded() {
    Watchdog wd(std::chrono::seconds(30), "multithreaded transfer must complete");
    constexpr int cnt = 20000;
    std::atomic<int> count = {0};
    std::atomic<long> sum = {0};
    std::atomic<bool> order_ok = {true};
    std::atomic<bool> single_thread = {true};

    auto q = std::make_shared<QueueAsStream<int>>();
    ExecutionWorker wrk(ThreadExecutor::create());
    auto pending = wrk.launch(mt_consumer(q, count, sum, order_ok, single_thread,
                                          std::this_thread::get_id()));

    //this thread plays the adapter - it publishes at full speed and never waits for the strategy
    int rejected = 0;
    for (int i = 0; i < cnt; ++i) if (!q->publish(i).has_value()) ++rejected;
    q->close();

    CHECK_EQUAL(rejected, 0);   //the publisher must never be refused while the receiver reads

    coro::sync_await(pending);

    CHECK(order_ok.load());
    CHECK(single_thread.load());
    CHECK_EQUAL(count.load(), cnt);
    CHECK_EQUAL(sum.load(), static_cast<long>(cnt) * (cnt - 1) / 2);
}

void test_iterator_as_stream() {
    auto stream = EventStream<int>(std::make_shared<IteratorAsStream<std::vector<int> > >(std::vector<int>{1,2,3,4}));
    int x;
    CHECK(stream.is_open());
    stream.receive(x);
    CHECK(stream.is_open());
    CHECK_EQUAL(x, 1);
    stream.receive(x);
    CHECK(stream.is_open());
    CHECK_EQUAL(x, 2);
    stream.receive(x);
    CHECK(stream.is_open());
    CHECK_EQUAL(x, 3);
    stream.receive(x);
    CHECK_EQUAL(x, 4);
    CHECK(!stream.is_open());


}


int main() {
    //one executor for the whole file - BacktestExecutor::create() cannot be called twice per thread
    auto wrk = BacktestExecutor::create();
    auto ewrk = ExecutionWorker(wrk);

    test_basic(ewrk, wrk);
    test_drain_after_close(ewrk, wrk);
    test_fifo_order(ewrk, wrk);
    test_born_closed();
    test_close_releases_receiver(ewrk, wrk);
    test_receiver_drop_expires_weak_ptr(ewrk, wrk);
    test_stop_token(ewrk, wrk);
    test_move_only(ewrk, wrk);
    test_multithreaded();
    test_iterator_as_stream();
    std::cout << "All tests passed" << std::endl;
}
