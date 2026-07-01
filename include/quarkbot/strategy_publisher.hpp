#pragma once

#include "basic_coro/cancel_signal.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "event_stream.hpp"
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
namespace quarkbot {

///StrategyPublisher is general purpose publisher for strategy internal use. It allows to publish events and subscribe to them.
/**
   The publisher can be shared between multiple execution workers. It is MT safe. 
   Multiple subscribers can subscribe to the publisher. Each subscriber receives own stream object (EventStream<T>).
   The EventStream<T> can be read by one consumer. If you need multiple consumers, you need to create multiple subscribers.
   Multiple producers can publish to the publisher. The publisher guarantees that all events are published in order.
   The publisher can be used to implement custom event streams, which are not provided by the exchange


    @tparam T type of events to publish
 */
template<typename T>
class StrategyPublisher : public std::enable_shared_from_this<StrategyPublisher<T> >{
public:

    using Revision = std::size_t;

    ///Create new publisher with given maximum queue length. If the queue length is exceeded, the oldest event is dropped.
    /**
     * @param max_queue_length maximum length of the queue
     * @return shared pointer to the created publisher  
     * @note do not call constructor directly, use create() function to create shared pointer to the publisher. The publisher is designed to be used as shared pointer, so it can be safely shared between multiple execution workers.
     */
    static std::shared_ptr<StrategyPublisher<T> > create(std::size_t max_queue_length = 1) {
        auto r =  std::make_shared<StrategyPublisher<T> >();
        r->_max_queue_length = max_queue_length;
        return r;
    }

    ///Subscribe to the publisher. Each subscriber receives own stream object (EventStream<T>).
    /**
     * @return EventStream object which wraps arount the unique pointer to the subscriber. 
     * @note the stream object is designed to be used as unique pointer, so it can be safely used by one consumer. 
                Do not share the stream object between multiple consumers. If you need multiple consumers, you need to 
                create multiple subscribers by calling subscribe() multiple times.

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
        if (rev == _current_revision) return false;
        std::scoped_lock lk(_mx);
        if (rev == _current_revision) return false;
        rev = std::max(_current_revision - _queue.size(), rev);
        to = _queue[_current_revision - rev];        
        ++rev;
        return true;
    }


    ///await support for subscriber
    /**
        @param to reference to store the read value
        @param rev reference to store the revision of the read value. Value is updated when successful read.
        @param awaiting the result of awaiting operation. The awaiting operation is completed when a new value is published or when the publisher is closed. The awaiting operation can be canceled by canceling the cancel signal passed to the function. If the awaiting operation is completed because a new value is published, the function returns a prepared coroutine which can be used to resume the awaiting coroutine. If the awaiting operation is completed because the publisher is closed, the function returns a prepared coroutine which can be used to resume the awaiting coroutine with false. If the awaiting operation is canceled, the function returns a prepared coroutine which can be used to resume the awaiting coroutine with false.
        @param cancel_sig optional pointer to cancel signal. If provided, it serves as identification and also helps to properly interrupt awaiting operation. Use cancel function to alert the signal. If this signal is set to true when function is called, the function returns immediately with prepared coroutine which can be used to    resume the awaiting coroutine with false.

        @return function returns coro::prepared_coro which should be resumed to complete operation. However, 
                 the returned value can be empty in situation, when operation is pending. So you 
                 can detect such case and use it in advantage. See subscriber implementation for example.
    */
    auto await(T &to, Revision &rev, awaitable<bool>::result awaiting, coro::cancel_signal *cancel_sig) {
        coro::prepared_coro p;
        std::scoped_lock lk(_mx);
        if (rev == _current_revision) {
            if (_closed || (cancel_sig && cancel_sig->is_canceled()))  {
                p = awaiting(false);
            } else {
                _awaiting.emplace_back(std::move(awaiting), IExecutionWorker::current(), cancel_sig, &to, &rev);                
            }
        } else {            
            rev = std::max(_current_revision - _queue.size(), rev);
            to = _queue[_current_revision - rev];        
            ++rev;
            p = awaiting(true);
        }        
        return p;
    }

    ///Publish a new value to the publisher. All subscribers will receive the new value in order. If the queue length is exceeded, the oldest event is dropped.
    /**
        @param val value to publish
        @return vector of prepared coroutines which should be resumed to complete awaiting operations of subscribers.
            if the return value is dropped, then resume operation is performed automatically. You
            can use return value to schedule the resumption of awaiting coroutines in more efficient way (for example leave critical section before resuming coroutines)
            Subscribers registered with worker are not resumed here (they are scheduled to be resumed by their workers).
    */
    auto publish(T val) {
        std::vector<coro::prepared_coro> ready;
        {
            std::scoped_lock lk(_mx);
            _queue.push_back(std::move(val));
            if (_queue.size()>_max_queue_length) _queue.pop_front();
            ++_current_revision;
            for (auto &a: _awaiting) {
                *a.target = val;
                *a.target_rev = _current_revision;
                auto cor = a.resolve(true);
                if (cor){
                    ready.push_back(std::move(cor));
                }
            }
            _awaiting.clear();            
        }
        return ready;
    }

    ///cancel awaiting operation on specified cancel signal which identifies the subscriber.
    /**
        This is called to close subscriber. The subscriber is closed when the cancel signal is set to true. The function cancels all awaiting operations of the subscriber identified by the cancel signal and removes them from the awaiting list. The function returns true if any awaiting operation was canceled, false otherwise.
        @param cancel_sig cancel signal which identifies the subscriber to cancel
        @retval true awaiting operation was canceled
        @retval false no awaiting operation was canceled (subscriber was not found, is already done or currently not awaiting).
                    In this case it will be canceled on next co_await call.
    */
    bool cancel(coro::cancel_signal *cancel_sig) {
        coro::prepared_coro p;
        std::scoped_lock lk(_mx);
        auto iter = std::find_if(_awaiting.begin(), _awaiting.end(), [&](const Result &res){
            return res.cancel_sig == cancel_sig;
        });
        if (iter == _awaiting.end()) return false;
        iter->resolve(false);
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
        for (auto &a: _awaiting) {
            auto p = a.resolve(false);
            if (p) coros.push_back(std::move(p));
        }
        return coros;
    }

    ///Check if the publisher is open (not closed).
    bool is_open() const {
        return !_closed;
    }
    

protected:


    struct Result {
        awaitable<bool>::result result;
        ExecutionWorker worker;//worker is optional here
        coro::cancel_signal *cancel_sig;
        T *target;
        Revision *target_rev;

        coro::prepared_coro resolve(bool value) {
            coro::prepared_coro out;
            if (worker) worker.resume(result(value));
            else out = result(value);
            return out;
        }
    };

    mutable std::mutex _mx;
    Revision _current_revision = 0;
    std::deque<T> _queue = {};
    std::size_t _max_queue_length = 0;
    std::vector<Result> _awaiting;
    bool _closed = false;

    class Subscriber: public IEventStream<T>{
    public:
        Subscriber(std::shared_ptr<StrategyPublisher>  publisher):_publisher(publisher) {}

        bool current(T &ref) override {
           return _publisher->read(ref, _rev);
        }

        coro::awaitable<bool> receive(T &ref) override {
            if (_publisher->read(ref, _rev)) {
                return true;
            }
            return [this,&ref](auto promise) {
                return  _publisher->await(ref, _rev, std::move(promise), &_csig);                

            };

        }
        coro::awaitable<bool> receive(T &ref, std::size_t &missed) override {
            Revision cur_rev = _rev;
            if (_publisher->read(ref, _rev)) {
                missed = _rev - cur_rev - 1;
                return true;
            }
            return [this, &ref, &missed](auto promise) {
                Revision cur_rev = 0;
                coro::prepared_coro p = _publisher->await(ref, _rev, std::move(promise), &_csig);
                //if p is filled, no awaiting has been done (resume current coroutine)
                //we can safely check revision difference to see what happened
                if (p && cur_rev < _rev) missed = _rev - cur_rev - 1;                                    
                return p;
            };
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
    };

};

    
}