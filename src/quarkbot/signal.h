#pragma once

#include "common.h"
#include "function.h"
#include "awaiter.h"

#include <deque>
namespace quarkbot {

///Helper class to synchronize multiple coroutines at single point
/**
 *
 * @tparam T type of value which is sent with the signal. This is also result
 * of co_await operation
 * @tparam Lock Allows to specify lock object. Default is NoLock which limits
 * usage of this class in single thread environment. You can specify std::mutex
 * to use the function in multi-threaded environment
 *
 * @code
 * coro wait_for_signal(Signaller<int> &s) {
 *      int r = co_await s.wait();
 *      //signal received with value r
 * }
 * @endcode
 *
 *
 */
template<typename T = void, typename Lock = NoLock>
class Signaller {
public:

    using Awaitable = AwaitableResult<T, Function<void(Function<void(AsyncResult<T>)>)> >;

    ///Send a signal
    /**
     * @param args arguments need to construct signal value
     * @retval true no awaiters left
     * @retval false there are still awaiters (registered during signal processing)
     *
     * @note all awaiting coroutines are executed in current thread
     */
    template<typename ... Args> requires(std::is_constructible_v<AsyncResult<T>, Args...>)
    bool send(Args && ... args) {
        std::unique_lock lk(_mx);
        if (_cblist.empty()) return true;
        send_internal(lk, AsyncResult<T>(std::forward<Args>(args)...));
        return _cblist.empty();
    }
    ///Send exception
    /**
     * @param e exception to send. Causes thrown this exception in coroutines
     * @note all awaiting coroutines are executed in current thread
     * @retval true no awaiters left
     * @retval false there are still awaiters (registered during signal processing)
     */
    bool send_exception(std::exception_ptr e) {
        std::unique_lock lk(_mx);
        if (_cblist.empty()) return true;
        send_internal(lk, AsyncResult<T>(e));
        return _cblist.empty();
    }

    ///Send current exception to all coroutines
    /**
     * @retval true no awaiters left
     * @retval false there are still awaiters (registered during signal processing)
     */
    bool send_exception() {
        std::unique_lock lk(_mx);
        if (_cblist.empty()) return true;
        send_internal(lk, AsyncResult<T>(std::current_exception()));
        return _cblist.empty();
    }


    ///Determines whether there are awaiters
    /**
     * @retval true no awaiters left
     * @retval false there are awaiters
     */
    bool empty() const {
        std::unique_lock lk(_mx);
        return _cblist.empty();
    }

    ///Await on signal
    /**
     * @return awaitable object, you can use co_await. If you need to wait
     * on signal in non-coroutine function, you can attach a callback by using
     * operator >>
     *
     * @code
     * signaller.wait() >> [=](AsyncResult<T> data) {
     *      //process signal value
     * }
     * @endcode
     */
    Awaitable wait() {
        return [this](auto &&fn) {
            std::lock_guard _(_mx);
            _cblist.emplace_back();
            std::construct_at(&_cblist.back()._fn, std::move(fn));
        };
    }

    ///register a callback to a signal
    /**
     * @param cb callback function
     * @retval true was empty previously
     * @retval false wasn't empty previously
     */
    template<std::invocable<AsyncResult<T> > CB>
    bool register_callback(CB  &&cb) {
        std::lock_guard _(_mx);
        bool r = _cblist.empty();
        _cblist.emplace_back();
        std::construct_at(&_cblist.back()._cb, std::move(cb));
        return r;
    }

    ///Clears all awaiters, co_await operations are canceled
    void clear() {
        std::unique_lock lk(_mx);
        while (!_cblist.empty()) {
            auto &item = _cblist.front();
            lk.unlock();
            std::destroy_at(&item._cb);
            lk.lock();
            _cblist.pop_front();
        }
    }


    ~Signaller() {
        clear();
    }

protected:
    union Item {
        Function<void(AsyncResult<T>)> _cb;
        Item() {}
        ~Item() {}
    };
    std::deque<Item>_cblist;
    mutable Lock _mx;

    void send_internal(std::unique_lock<Lock> &lk, AsyncResult<T> &&val) noexcept {
        for (std::size_t i = 0, sz = _cblist.size();i<sz; ++i) {
            auto &item = _cblist.front();
            lk.unlock();
            item._cb(val);
            std::destroy_at(&item._cb);
            lk.lock();
            _cblist.pop_front();
        }
    }


};

}
