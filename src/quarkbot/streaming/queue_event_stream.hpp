#pragma  once

#include "basic_coro/prepared_coro.hpp"
#include "quarkbot/utils/stack_vector.hpp"
#include <quarkbot/abstract/ieventstream.hpp>
#include <quarkbot/execution_worker.hpp>
#include <algorithm>
#include <memory>
#include <mutex>
namespace quarkbot {


template<typename T>
class QueueEventStream: public EventStreamStoppable<T> {
public:

    QueueEventStream(std::function<void()> unsub_fn): _unsub_fn(std::move(unsub_fn) ) {}
    ~QueueEventStream() {
        close();
    }

    coro::prepared_coro push(T &&val) {
        std::scoped_lock _(_mx);        
        if (_closed) return {};
        if (_awaiting_value) {
            *_awaiting_value = std::move(val);
            return _awaiting(true);
        } else {
            _q.push(std::move(val));
        }
        return {};

    }
    coro::prepared_coro push(const T &val) {
        std::scoped_lock _(_mx);        
        if (_closed) return {};
        if (_awaiting_value) {
            *_awaiting_value = val;
            return _awaiting(true);
        } else {
            _q.push(val);
        }
        return {};
    }

    virtual bool is_open() const override {
        std::scoped_lock _(_mx);
        return !_closed;
    }
    virtual void close() override {
        std::scoped_lock _(_mx);
        if (!_closed) {
            _unsub_fn();
            _unsub_fn = nullptr;
            _closed = true;
        }
    }


    virtual bool current(T &) override {
        return false; //queue doesn't support this function
    }

    virtual coro::awaitable<bool> receive(T &ref) override {
        std::scoped_lock _(_mx);
        if (!_q.empty()) {
            ref = std::move(_q.front());
            _q.pop();
            return true;
        }
        if (_closed) return false;
        return [&ref, this](auto promise) {
            coro::prepared_coro out;
            std::scoped_lock _(_mx);
            if (_closed) {
                out = promise(false);
            } else if (!_q.empty()) {
                ref = std::move(_q.front());
                _q.pop();
                out = promise(true);
            } else {
                _awaiting_value = &ref;
                _awaiting = std::move(promise);                
            }
            return out;
        };
        
    }

    virtual coro::awaitable<bool> receive(T &ref, std::size_t &missed) override {
        missed = 0;
        return receive(ref);
    }
  
protected:

    mutable std::mutex _mx;
    std::queue<T> _q;
    awaitable<bool>::result _awaiting = {};
    T *_awaiting_value = nullptr;
    bool _closed;
    std::function<void()> _unsub_fn;


};

template<typename T>
class QueueEventPublisher : public std::enable_shared_from_this<QueueEventPublisher<T> >{
public:

    std::shared_ptr<IEventStream<T> > create_subscriber() {
        std::scoped_lock _(_mx);
        auto id = _next_id++;
        auto sub =  std::make_shared<QueueEventStream<T> >([me = this->shared_from_this(), id]{
            me->unsubscribe(id);
        });
        _subscribers.emplace_back(id, sub.get());
        return sub;
    }

    void publish(const T &val) {
        StackVector<coro::prepared_coro, 32> out;
        std::scoped_lock _(_mx); 
        out.reserve(_subscribers.size());
        for (auto &[id, sub]: _subscribers) {
            out.emplace_back(out, sub->push(val));
        }
    }

protected:
    std::mutex _mx;
    std::size_t _next_id = 1;
    std::vector<std::pair<std::size_t, QueueEventStream<T> * > > _subscribers;

    void unsubscribe(std::size_t id) {
        std::scoped_lock _(_mx);
        auto iter = std::find_if(_subscribers.begin(), _subscribers.end(), [&](const auto &p){
            return p.first == id;
        });
        if (iter != _subscribers.end()) {
            _subscribers.erase(iter);
        }
    }
};


}