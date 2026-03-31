#pragma once
#include "coro/src/basic_coro/cancel_signal.hpp"
#include "coro/src/basic_coro/awaitable.hpp"
#include "coro/src/basic_coro/prepared_coro.hpp"
#include <algorithm>
#include <array>
#include <concepts>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>



template<typename T, std::size_t limit, typename Lock = std::mutex>
class Subscriber;

///Publisher implements simple publish-subscribe pattern with fixed size queue
/**
 * @tparam T type of published item
 * @tparam limit maximum number of stored items in the queue
 * @tparam Lock type of lock used for synchronization
 */
template<typename T, std::size_t limit, typename Lock = std::mutex>
class Publisher {
public:

    using Subs = Subscriber<T, limit, Lock>;
    using Cursor = std::size_t;
    using value_type = T;

    ///Publish value
    void post(const T &val) {
        std::unique_lock<Lock> lk(_mx);
        post(val, lk);
    }

    ///Publish value
    void post(T &&val) {
        std::unique_lock<Lock> lk(_mx);
        post(std::move(val), lk);
    }


    ///Read value at cursor position
    /**
    @param cur cursor position
    @param cb callback called with pointer to read value or nullptr if there is no value at given cursor
    @note cursor points to position in the queue. The cursor is incremented automatically when reading. If there is no value at given cursor,
    the callback is called with nullptr. This allows to read value directly into user-provided storage to avoid copies.
    @return callback return value
     */
    auto read(Cursor &cur, std::invocable<const T *> auto cb) {
        std::lock_guard<Lock> lk(_mx);
        if (_top == cur) return cb(nullptr);
        auto dist = (_top - cur)-1;        
        if (dist >= _queue.size()) {            
            cur = _top - _queue.size() + 1;
            return cb(&_queue.back());
        } else {
            auto pos = _queue.begin();
            std::advance(pos, dist);
            ++cur;
            return cb(&(*pos));
        }
    }


    ///Read value at cursor position
    /**
    @param cur cursor position
    @return optional containing read value or empty optional if there is no value at given cursor
     */
    std::optional<T> read(Cursor &cur) {
        std::optional<T> out;
        read(cur, [&out](const T *val) {
            if (val) out.emplace(*val);
        });
        return out;
    };
    
    ///Await for new value at given cursor position
    /**
    @param subs subscriber object
    @param cur cursor position
    @param cancel_signal optional alert flag to prevent subscription upon cancellation
    @retval true subscription accepted
    @retval false value is already available or publisher is closed or subscription is cancelled
     */
    bool await(std::shared_ptr<Subs> subs, Cursor cur, coro::cancel_signal *cancel_signal = nullptr)  {
        std::lock_guard _(_mx);
        if (cur != _top || _closed) return false;
        if (cancel_signal && *cancel_signal) return false;
        _awaiting.push_back(std::move(subs));
        return true;
    }

    ///Kick subscriber - remove from awaiting list
    /**
    @param subs subscriber to remove
    @param cancel_signal optional alert flag to set upon removal (atomically).This prevents future subscriptions 
    that may happen concurrently.
     */
    void kick(std::shared_ptr<Subs> subs, coro::cancel_signal *cancel_signal = nullptr) {
        std::lock_guard _(_mx);
        auto e = std::remove_if(_awaiting.begin(), _awaiting.end(), [&](const std::weak_ptr<Subs> &item){
            return item.lock() == subs;
        });
        _awaiting.erase(e, _awaiting.end());
        if (cancel_signal) cancel_signal->request_cancel();        
    }

    ///Check if value is available at given cursor
    bool available(Cursor cur) const {
        std::lock_guard _(_mx);
        return cur != _top || _closed;
    }

    ///Check if publisher is closed
    bool is_closed() const {
        std::lock_guard _(_mx);
        return _closed;
    }

    ///Get current top cursor
    Cursor get_top_cursor() const {
        std::lock_guard _(_mx);
        return _top;
    }

    ///Close publisher - no more items are published (EOF)
    void close() {
        std::unique_lock lk(_mx);
        _closed = true;
        on_post(lk);
    }


protected:
    mutable Lock _mx;
    Cursor _top;
    std::deque<T> _queue;
    std::vector<std::weak_ptr<Subs> > _awaiting;
    bool _closed = false;


    void on_post(std::unique_lock<Lock> &lk, std::size_t ofs = 0, std::size_t wr_ofs =0) {        
        std::array<std::shared_ptr<Subs>, 32> rdlst = {};
        std::size_t pos = 0;
        while (pos < rdlst.size() && ofs < _awaiting.size()) {
            rdlst[pos] = _awaiting[ofs].lock();
            if (rdlst[pos] == nullptr) {
                ++ofs;
            } else {
                _awaiting[wr_ofs] = _awaiting[ofs];
                ++ofs;
                ++wr_ofs;
                ++pos;
            }
        }
        if (ofs == _awaiting.size()) {
            while (_queue.size()>limit) _queue.pop_back();
            lk.unlock();
            _awaiting.resize(wr_ofs);
        } else {
            on_post(lk, ofs, wr_ofs);
        }
        for (std::size_t i = 0; i < pos; ++i) {
            rdlst[i]->notify();
        }

    }
    void post(const T &val, std::unique_lock<Lock> &lk) {
        _queue.push_front(val);
        on_post(lk);       
    }
    void post(T &&val, std::unique_lock<Lock> &lk) {
        _queue.push_front(std::move(val));
        on_post(lk);       
    }
};

template<typename T, std::size_t limit, typename Lock>
class Subscriber : public std::enable_shared_from_this<Subscriber<T, limit, Lock> >{
public:

    using Pubs = Publisher<T, limit, Lock>;
    using Cursor = typename Pubs::Cursor;
    using value_type = T;

    ///Read next value
    /**
    @return awaitable returning optional containing read value or empty optional if publisher is closed. 
    Use with co_await. It blocks until new value is published or publisher is closed.

    @note only one read can be pending at a time
     */
    coro::awaitable<T> read() {
        if (!_source) return std::nullopt;
        if (_source->available(_cur)) {
            return _source->read(_cur,[](const T *val){
                if (val) {
                  return coro::awaitable<T>(*val);
                } else {
                    return coro::awaitable<T>(std::nullopt);
                }
            });
        } else {
            return [this](typename coro::awaitable<T>::result prom) -> coro::prepared_coro{
                _result = std::move(prom);
                if (!_source->await(this->shared_from_this(), _cur, &_cancel_signal)) {
                    return _source->read(_cur, [this](const T *val){
                        if (val) {
                            return _result(*val);
                        } else {
                            return _result(std::nullopt);
                        }
                    });
                }
                return {};
            };
        }
        
    }

    ///Subscribe to publisher
    void subscribe(Pubs &publisher) {
        _source = &publisher;
        _cancel_signal.reset();
        _cur = _source->get_top_cursor();
    }

    ///Close subscriber - unsubscribe from publisher, unblocking any awaiting read
    void close() {
        if (_source) {
            _source->kick(this->shared_from_this(),&_cancel_signal);
        }
    }

    ///Check if subscriber is closed
    bool is_closed() const {
        return static_cast<bool>(_cancel_signal);
    }

protected:

    Pubs *_source;
    coro::cancel_signal _cancel_signal;
    Cursor _cur = 0;
    coro::awaitable<T>::result _result;
    void notify() {

    }

    friend class Publisher<T,limit,Lock>;

};


