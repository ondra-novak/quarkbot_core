#pragma once
#include <thread>
#include <condition_variable>

#include "dispatcher.h"

class DispatcherBase {
protected:
    static thread_local DispatcherBase *_current;
};


template<std::invocable<> T,typename Hasher =  std::hash<T>, typename Compare = std::equal_to<T> >
class DispatcherThread: public DispatcherCore<T, Hasher, Compare>, public DispatcherBase {
public:

    using Super = DispatcherCore<T, Hasher,Compare>;
    using TimePoint = typename Super::TimePoint;
    using Ident = typename Super::Ident;

    ~DispatcherThread() {
        stop();
    }

    template<typename ... Args> requires(std::is_constructible_v<T, Args...>)
    void post(Args && ... args) {
        std::lock_guard _(_mx);
        if (Super::post(std::forward<Args>(args)...)) {
            notify();
        }
    }

    template<typename ... Args> requires(std::is_constructible_v<T, Args...>)
    void post_collapse(Args && ... args) {
        std::lock_guard _(_mx);
        if (Super::post_collapse(std::forward<Args>(args)...)) {
            notify();
        }
    }

    template<typename ... Args>
    void post_timed(Ident ident, TimePoint tp, Args && ... args ) {
        std::lock_guard _(_mx);
        if (Super::post_timed(ident, tp, std::forward<Args>(args)...)) {
            notify();
        }
    }

    template<typename ... Args>
    void post_timed(TimePoint tp, Args && ... args) {
        std::lock_guard _(_mx);
        if (Super::post_timed(tp, std::forward<Args>(args)...)) {
            notify();
        }
    }

    bool cancel(Ident ident) {
        std::unique_lock lk(_mx);
        bool r = Super::cancel(ident);
        if (r == false) {
            if (this->_executing_ident == ident) {
                _awaiting = true;
                _cond.wait(lk, [this, ident]{return this->_executing_ident != ident;});
                return true;
            }
        }
        return r;
    }

protected:
    std::mutex _mx;
    std::thread _wrk;
    std::condition_variable _cond;
    bool _stopped = false;
    bool _awaiting = false;

    void worker() {

        _current = this;
        std::unique_lock lk(_mx);

        auto executor = [this,&lk](T &&x) noexcept {
            lk.unlock();
            x();
            if (DispatcherBase::_current != this) return false;
            lk.lock();
            return true;
        };

        while (!_stopped) {
            if (!this->process_message(executor)) {
                if (DispatcherBase::_current != this) return;
                auto nx = this->get_nearest_schedule();
                if (nx == TimePoint::max()) _cond.wait(lk);
                else _cond.wait(lk,nx);
            }
            if (_awaiting) {
                _awaiting = false;
                _cond.notify_all();
            }
        }
    }

    void notify() {
        if (!_wrk.joinable()) {
            _wrk = std::thread([this]{worker();});
        } else {
            _cond.notify_all();
        }
    }

    void stop() {
        if (_wrk.joinable()) {
            if (std::this_thread::get_id() == _wrk.get_id()) {
                _wrk.detach();
                this->_current = nullptr;
                return;
            } else {
                std::lock_guard _(_mx);
                _stopped = true;
            }
            _cond.notify_all();
            _wrk.join();
        }
    }
};

inline thread_local DispatcherBase *DispatcherBase::_current = nullptr;
