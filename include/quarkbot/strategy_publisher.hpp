#pragma once

#include "basic_coro/cancel_signal.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "event_stream.hpp"
#include "quarkbot/abstract/ieventstream.hpp"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
namespace quarkbot {

///StrategyPublisher is general purpose publisher for strategy internal use. It allows to publish events and subscribe to them.
/**
   The publisher can be shared between multiple execution workers. It is MT safe.
   Multiple subscribers can subscribe to the publisher. Each subscriber receives own stream object (EventStream<T>).
   The EventStream<T> can be read by one consumer. If you need multiple consumers, you need to create multiple subscribers.
   Multiple producers can publish to the publisher. The publisher guarantees that all events are published in order.
   The publisher can be used to implement custom event streams, which are not provided by the exchange

   There is no backpressure. The publisher never blocks the producer. If a subscriber doesn't read fast enough,
   the oldest events are dropped and the subscriber is informed about count of dropped events through
   the "missed" argument of receive().

    @tparam T type of events to publish
 */
template<typename T>
class StrategyPublisher : public std::enable_shared_from_this<StrategyPublisher<T> >{
public:

    using Revision = std::size_t;

    ///Construct publisher with given maximum queue length
    /**
     * @param max_queue_length maximum length of the queue (at least 1)
     * @note prefer create() which returns the publisher already wrapped in a shared pointer
     */
    explicit StrategyPublisher(std::size_t max_queue_length = 1)
        :_max_queue_length(std::max<std::size_t>(1, max_queue_length)) {}

    ///Create new publisher with given maximum queue length. If the queue length is exceeded, the oldest event is dropped.
    /**
     * @param max_queue_length maximum length of the queue
     * @return shared pointer to the created publisher
     * @note do not call constructor directly, use create() function to create shared pointer to the publisher. The publisher is designed to be used as shared pointer, so it can be safely shared between multiple execution workers.
     */
    static std::shared_ptr<StrategyPublisher<T> > create(std::size_t max_queue_length = 1) {
        return std::make_shared<StrategyPublisher<T> >(max_queue_length);
    }

    ///Subscribe to the publisher. Each subscriber receives own stream object (EventStream<T>).
    /**
     * @return EventStream object which wraps arount the unique pointer to the subscriber.
     * @note the stream object is designed to be used as unique pointer, so it can be safely used by one consumer.
                Do not share the stream object between multiple consumers. If you need multiple consumers, you need to
                create multiple subscribers by calling subscribe() multiple times.

        @note the new subscriber starts at the oldest event still held in the queue, so it can receive
                events published before the subscription (at most max_queue_length of them). This is
                consistent with the other publishers in the library.

        @note Destruction of the stream. If the stream is to be destroyed outside reader's coroutine, it must
        be closed. It is recommended to destroy stream in the reader's coroutine and use close() to signal such a request,

     */
    EventStream<T> subscribe() {
        auto r = std::make_unique<Subscriber>(this->shared_from_this());
        return EventStream<T>(std::move(r));
    }

    ///manual read - support for subscriber
    /**
        * @param to reference to store the read value
        * @param rev reference to store the revision of the read value. The revision is incremented on each publish. The subscriber can use the revision to detect if it has missed any events. If the revision is equal to the current revision, it means that the subscriber has not received any new events since the last read. If the revision is less than the current revision, it means that the subscriber has missed some events. The subscriber can use the revision to calculate how many events it has missed.
        * @return true if a new value was read, false if there are no new values (the revision is equal to the current revision)

        * @note use streams for more convenient and efficient way to read the values.
    */
    bool read(T &to, Revision &rev) const {
        if (rev >= _current_revision.load(std::memory_order_acquire)) return false;
        std::scoped_lock lk(_mx);
        const Revision cur = _current_revision.load(std::memory_order_relaxed);
        if (rev >= cur || _queue.empty()) return false;
        //the queue holds revisions <cur - _queue.size(), cur), the oldest one at index 0
        const Revision oldest = cur - _queue.size();
        if (rev < oldest) rev = oldest;     //the subscriber missed some events
        to = _queue[rev - oldest];
        ++rev;
        return true;
    }


    ///await support for subscriber
    /**
        @param rev revision the subscriber is standing at. If the current revision is higher, the operation
                is resolved with true immediately, otherwise the subscriber is registered as awaiting.
        @param awaiting the result of awaiting operation. The awaiting operation is completed when a new value is published or when the publisher is closed. The awaiting operation can be canceled by canceling the cancel signal passed to the function. If the awaiting operation is completed because a new value is published, the function returns a prepared coroutine which can be used to resume the awaiting coroutine. If the awaiting operation is completed because the publisher is closed, the function returns a prepared coroutine which can be used to resume the awaiting coroutine with false. If the awaiting operation is canceled, the function returns a prepared coroutine which can be used to resume the awaiting coroutine with false.
        @param cancel_sig optional pointer to cancel signal. If provided, it serves as identification and also helps to properly interrupt awaiting operation. Use cancel function to alert the signal. If this signal is set to true when function is called, the function returns immediately with prepared coroutine which can be used to    resume the awaiting coroutine with false.

        @return function returns coro::prepared_coro which should be resumed to complete operation. However,
                 the returned value can be empty in situation, when operation is pending. So you
                 can detect such case and use it in advantage. See subscriber implementation for example.

        @note the function only signals the availability of a value, it never touches subscriber's memory.
                The value itself is picked up by the subscriber in destructor of its callback object, which
                is called exactly when the awaiting operation is being resolved (@see Subscriber::ReceiveHandler).
                Because of that, resolving is done while _mx is held and the subscriber reenters read() -
                that is why _mx is recursive.
    */
    coro::prepared_coro await(Revision rev, awaitable<bool>::result awaiting, coro::cancel_signal *cancel_sig) {
        std::scoped_lock lk(_mx);
        if (cancel_sig && cancel_sig->is_canceled()) return awaiting(false);
        if (rev < _current_revision.load(std::memory_order_relaxed)) return awaiting(true);
        if (_closed) return awaiting(false);
        _awaiting.push_back(Result{std::move(awaiting), cancel_sig});
        return {};
    }

    ///Publish a new value to the publisher. All subscribers will receive the new value in order. If the queue length is exceeded, the oldest event is dropped.
    /**
        @param val value to publish
        @return vector of prepared coroutines which should be resumed to complete awaiting operations of subscribers.
            if the return value is dropped, then resume operation is performed automatically. You
            can use return value to schedule the resumption of awaiting coroutines in more efficient way (for example leave critical section before resuming coroutines)

        @note publishing to a closed publisher is ignored
    */
    auto publish(T val) {
        std::vector<coro::prepared_coro> ready;
        std::scoped_lock lk(_mx);
        if (_closed) return ready;
        _queue.push_back(std::move(val));
        if (_queue.size()>_max_queue_length) _queue.pop_front();
        _current_revision.store(_current_revision.load(std::memory_order_relaxed)+1,
                                std::memory_order_release);
        ready.reserve(_awaiting.size());
        for (auto &a: _awaiting) {
            //resolving reads the value from the queue in subscriber's callback destructor
            auto cor = a.resolve(true);
            if (cor){
                ready.push_back(std::move(cor));
            }
        }
        _awaiting.clear();      //keeps the capacity for the next round
        //the lock is released before the caller resumes the returned coroutines
        return ready;
    }

    ///cancel awaiting operation on specified cancel signal which identifies the subscriber.
    /**
        This is called to close subscriber. The cancel signal is set, so the subscriber will not await again.
        The function cancels the awaiting operation of the subscriber identified by the cancel signal and removes
        it from the awaiting list. The function returns true if any awaiting operation was canceled, false otherwise.
        @param cancel_sig cancel signal which identifies the subscriber to cancel
        @retval true awaiting operation was canceled
        @retval false no awaiting operation was canceled (subscriber was not found, is already done or currently not awaiting).
                    The cancel signal is set anyway, so the next co_await returns false.
    */
    bool cancel(coro::cancel_signal *cancel_sig) {
        if (!cancel_sig) return false;
        coro::prepared_coro p;   //destroyed (=resumed) after the lock is released
        std::scoped_lock lk(_mx);
        cancel_sig->request_cancel();
        auto iter = std::find_if(_awaiting.begin(), _awaiting.end(), [&](const Result &res){
            return res.cancel_sig == cancel_sig;
        });
        if (iter == _awaiting.end()) return false;
        p = iter->resolve(false);
        _awaiting.erase(iter);
        return true;
    }

    ///Close the publisher and cancel all awaiting operations.
    /**
        This function closes the publisher and cancels all awaiting operations. It returns a vector of prepared coroutines which should be resumed to complete the cancellation.
        @return vector of prepared coroutines which should be resumed to complete the cancellation.
        If result is dropped, then resume operation is performed automatically.
         You can use return value to schedule the resumption of awaiting coroutines in more efficient way
          (for example leave critical section before resuming coroutines)
    */
    auto close() {
        std::vector<coro::prepared_coro> coros;
        std::scoped_lock lk(_mx);
        if (_closed) return coros;
        _closed = true;
        coros.reserve(_awaiting.size());
        for (auto &a: _awaiting) {
            auto p = a.resolve(false);
            if (p) coros.push_back(std::move(p));
        }
        _awaiting.clear();
        return coros;
    }

    ///Check if the publisher is open (not closed).
    bool is_open() const {
        return !_closed.load(std::memory_order_acquire);
    }


protected:


    ///registration of an awaiting subscriber - it holds no pointer to subscriber's memory
    struct Result {
        awaitable<bool>::result result;
        coro::cancel_signal *cancel_sig;

        coro::prepared_coro resolve(bool value) {
            coro::prepared_coro out;
            out = result(value);
            return out;
        }
    };

    ///must be recursive - resolving an awaiting operation reenters read() @see await()
    mutable std::recursive_mutex _mx;
    std::atomic<Revision> _current_revision = {0};
    std::deque<T> _queue = {};
    std::size_t _max_queue_length = 1;
    std::vector<Result> _awaiting;
    std::atomic<bool> _closed = {false};

    class Subscriber: public EventStreamStoppable<T>{
    public:
        Subscriber(std::shared_ptr<StrategyPublisher>  publisher):_publisher(publisher) {}
        ~Subscriber() {
            //the registration identifies this object by its cancel signal, it must not outlive it
            _publisher->cancel(&_csig);
        }

        bool current(T &ref) override {
           Revision rev = _rev;
           if (_publisher->read(ref, rev)) {
                _rev = rev;
                return true;
           }
           if (_rev == 0) return false;
           rev = _rev - 1;      //re-read the last already received value
           return _publisher->read(ref, rev);
        }

        coro::awaitable<bool> receive(T &ref) override {
            if (_csig.is_canceled()) return false;
            if (_publisher->read(ref, _rev)) {
                return true;
            }
            return ReceiveHandler(this, ref, nullptr);
        }
        coro::awaitable<bool> receive(T &ref, std::size_t &missed) override {
            missed = 0;
            if (_csig.is_canceled()) return false;
            Revision cur_rev = _rev;
            if (_publisher->read(ref, _rev)) {
                missed = _rev - cur_rev - 1;
                return true;
            }
            return ReceiveHandler(this, ref, &missed);
        }

        void close() override {
            _publisher->cancel(&_csig);
        }

        bool is_open() const override{
            return !_csig.is_canceled() && _publisher->is_open();
        }


    protected:
        Revision _rev = {};
        std::shared_ptr<StrategyPublisher> _publisher = {};
        coro::cancel_signal _csig;

        ///requests the awaiting operation and picks the value up once the operation is resolved
        /**
         * The object is stored inside of the awaitable, in the space which is later reused for the
         * result. Because of that its destructor is called exactly at the moment when the result is
         * being set - which is the moment when the value is ready. So the value is read there. No
         * additional coroutine frame and no allocation is needed for this.
         */
        struct ReceiveHandler {
            Subscriber *_owner;
            T *_ref;
            std::size_t *_missed;   //optional

            ReceiveHandler(Subscriber *owner, T &ref, std::size_t *missed)
                :_owner(owner),_ref(&ref),_missed(missed) {}
            //moving the awaitable moves this object and destroys the source - it must stay silent then
            ReceiveHandler(ReceiveHandler &&other)
                :_owner(std::exchange(other._owner, nullptr)),_ref(other._ref),_missed(other._missed) {}
            ReceiveHandler &operator=(ReceiveHandler &&) = delete;

            ~ReceiveHandler() {
                //do not consume an event when the subscriber has been closed - the reader is gone
                if (_owner && !_owner->_csig.is_canceled()) {
                    const Revision prev = _owner->_rev;
                    if (_owner->_publisher->read(*_ref, _owner->_rev) && _missed) {
                        //nonzero only when events were dropped from the queue meanwhile
                        *_missed = _owner->_rev - prev - 1;
                    }
                }
            }

            coro::prepared_coro operator()(awaitable<bool>::result result) {
                return _owner->_publisher->await(_owner->_rev, std::move(result), &_owner->_csig);
            }
        };

        //the handler must fit into the awaitable, otherwise it would be allocated on the heap
        static_assert(sizeof(ReceiveHandler)
                <= std::max(coro::awaitable_reserved_space<bool>::value, sizeof(bool)),
                "ReceiveHandler must fit into awaitable<bool> without allocation");
    };

};


}
