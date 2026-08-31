#pragma once

#include "basic_coro/awaitable.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "quarkbot/abstract/ieventstream.hpp"
#include <mutex>
#include <optional>
#include <queue>
namespace quarkbot {


    ///declares event stream which is backed by queue. It is thread safe, can be used from multiple threads
    /**
        The stream connects one publisher with one receiver. Its purpose is to carry a data series
        produced asynchronously (typically a historical data query answered by an adapter) into a
        strategy, which can start processing while the download is still running.

        The queue is unbounded on purpose. The publisher is never blocked and never loses an event,
        so a slow receiver cannot slow the source down (sources are often latency critical - a slow
        consumer can cost the connection). The price paid for this is memory: the queue grows to the
        size of the unconsumed part of the series.

        Publishing never runs receiver's code. The awaiting coroutine is resumed through the
        proxy frame installed by Async/StrategyFragment (see TransformedAwaiterExecWorker), which
        only reschedules the coroutine on its own ExecutionWorker. The publisher thread pays for an
        enqueue, nothing more.

        Lifetime: the publisher is expected to hold a weak_ptr, so the receiver terminates the
        transfer simply by dropping its shared_ptr. close() is not the primary way to end the
        stream - it exists so that a receiver blocked in receive() can be released (a timeout, for
        example) and can drop its instance afterwards.

        @tparam T type of event data. It must be movable. Copyable T is required only if
            publish(const T &) is used.

        @note single receiver only. Two coroutines awaiting receive() at the same time is not
            supported - the second one replaces the first one's promise, which resolves the first
            await as canceled.
    */
    template<typename T>
    class QueueAsStream: public EventStreamStoppable<T> {
    public:

        ///constructs stream. If closed is true, stream is closed and cannot be used
        /**
        @param closed if true, stream is closed and cannot be used, allows to create closed stream if data is not available
         */
        QueueAsStream(bool closed = false):_closed(closed) {}

        QueueAsStream(const QueueAsStream &) = delete;
        QueueAsStream &operator=(const QueueAsStream &) = delete;

        ///awaitable read of next event. If stream is closed, returns false. If event is available, returns true and copies it to ref
        /**
        @param ref reference to object where event data will be copied.
        @retval true new event is available and copied to ref
        @retval false stream is closed or no new event is available

        @note awaitable - if event is not available, coroutine is suspended until new event is published or stream is closed
        */
        virtual awaitable<bool> receive(T &ref) override {
            std::scoped_lock _(_mx);    //non-blocking read
            auto r = receive_nb(ref);
            if (r) return r.value();
            return [this, &ref](auto promise) {
                std::scoped_lock _(_mx); //blocking read
                auto r = receive_nb(ref);
                if (r) return promise(r.value());
                _awaiting = std::move(promise);
                _awt_value = &ref;
                return coro::prepared_coro{};
            };
        }
        ///awaitable read of next event. If stream is closed, returns false. If event is available, returns true and copies it to ref
        /**
        @param ref reference to object where event data will be copied.
        @param missed reference to variable where count of missed events will be stored (always zero for this stream)
        @retval true new event is available and copied to ref
        @retval false stream is closed or no new event is available
        @note awaitable - if event is not available, coroutine is suspended until new event is published or stream is closed
        */
        virtual coro::awaitable<bool> receive(T &ref, std::size_t &missed) override {
            missed = 0;     //always zero;
            return receive(ref);

        }
        ///non-blocking read of next event, if one is queued, and copy it to ref
        /**
        @param ref reference to object where event data will be copied.
        @retval true an event was available and was copied to ref
        @retval false the queue is empty (whether the stream is open or closed)

        @note this function is non-blocking, if no event is available, returns false

        @note unlike other streams, this one CONSUMES the event - the queue holds a series, not a
            last known state, so there is no "current value" to report repeatedly. The function
            behaves as try_receive(): use it to drain what is already downloaded without suspending.
        */
        virtual bool current(T &ref) override {
            std::scoped_lock _(_mx);
            auto r = receive_nb(ref);
            return r && r.value();  

        }
        
        ///returns true if a further event can still be received
        /**
        @retval true either the stream is not closed, or it is closed but events published before
            closing are still waiting in the queue
        @retval false the stream is closed and drained, receive() will never return true again

        This is the receiver side predicate - it answers "is it still worth reading". Closing the
        stream does not discard what was already published, so a receive loop driven by this
        function delivers the whole series.

        @note the publisher must not use this function to decide whether to publish. It reports
            true for a stream closed by the receiver as long as the queue is not empty, and even a
            correct answer is stale the moment it is returned. Use the return value of publish(),
            which tests and pushes atomically, or can_publish() for diagnostics.
        */
        virtual bool is_open() const override {
            std::scoped_lock _(_mx);
            return !_closed || !_queue.empty();
        }

        ///returns true if the stream still accepts published events
        /**
        @retval true publish() will accept an event
        @retval false the receiver closed the stream, publish() will fail

        This is the publisher side predicate. It allows the publisher to stop producing data nobody
        will read instead of growing the queue - the receiver may close the stream and keep the
        instance alive for a while, in which case dropping the weak_ptr is not enough of a signal.

        @note this is NOT the negation of is_open(). A closed stream holding undelivered events
            reports is_open() == true and can_publish() == false.
        @note the answer is only advisory - the receiver may close the stream right after it was
            returned. The return value of publish() is the only race free answer.
        */
        bool can_publish() const {
            std::scoped_lock _(_mx);
            return !_closed;
        }
        ///closes the stream. If coroutine is awaiting, it is resumed with false
        /**
        Events already published stay in the queue and are still delivered - only after they run
        out does receive() report false. Closing makes publish() fail from this point on.
        */
        virtual void close() override {
            coro::prepared_coro p;
            std::scoped_lock _(_mx);
            _closed = true;
            _awt_value = nullptr;
            p = _awaiting(false);
            //destructor resumes - after the lock is released
        }

        ///publishes new event to stream. If coroutine is awaiting, it is resumed with true otherwise event is stored in queue. If stream was closed, returns nullopt
        /**
        @param val event data to publish
        @return prepared coroutiner wrapped in optional. If return value is discarded, the coroutine is resumed immediately. You can use
            has_value() whether stream is still open. The actual value of the prepared_coro conatains handle only if there were awaiting coroutine.
        */ 
        std::optional<coro::prepared_coro> publish(const T &val) {
            std::scoped_lock _(_mx);
            if (_closed) return std::nullopt;
            if (_awaiting) {
                *_awt_value = val;
                _awt_value = nullptr;
                return _awaiting(true);
            }
            _queue.push(val);
            return coro::prepared_coro{};
        }
        ///publishes new event to stream. If coroutine is awaiting, it is resumed with true otherwise event is stored in queue. If stream was closed, returns nullopt
        /**
        @param val event data to publish
        @return prepared coroutiner wrapped in optional. If return value is discarded, the coroutine is resumed immediately. You can use
            has_value() whether stream is still open. The actual value of the prepared_coro conatains handle only if there were awaiting coroutine.
        */
        std::optional<coro::prepared_coro> publish(T &&val) {
            std::scoped_lock _(_mx);
            if (_closed) return std::nullopt;
            if (_awaiting) {
                *_awt_value = std::move(val);
                _awt_value = nullptr;
                return _awaiting(true);
            }
            _queue.push(std::move(val));
            return coro::prepared_coro{};

        }

    protected:
        mutable std::mutex _mx;
        std::queue<T> _queue;
        coro::awaitable<bool>::result _awaiting;
        T *_awt_value = nullptr;
        bool _closed = false;

        std::optional<bool> receive_nb(T &ref) {
            if (!_queue.empty()) {
                ref = std::move(_queue.front());
                _queue.pop();
                return true;
            } 
            if (_closed) return false;
            return std::nullopt;

        }
    };

}