#include "backtest_executor.hpp"

#include "../common/logger.hpp"


#include <chrono>
#include <coroutine>
#include <memory>
#include <stdexcept>

namespace quarkbot {




void BacktestExecutor::flush_queue() {
    while (!_dispatch_queue.empty()) {
        while (!_dispatch_queue.empty()) {
            auto p = std::move(_dispatch_queue.front());
            _dispatch_queue.pop();
            p.lazy_resume();
        }
        for (std::size_t i = 0, cnt=_idle_queue.size(); i<cnt;++i) {
            auto p = std::move(_idle_queue.front());
            _idle_queue.pop();
            p.lazy_resume();
        }
    }
}

std::shared_ptr<BacktestExecutor> BacktestExecutor::create() {
    auto cur = IExecutionWorker::current();
    auto me = std::dynamic_pointer_cast<BacktestExecutor>(cur);
    if (me) {
        if (cur) throw std::runtime_error("Thread is alread execution worker of different type");
        return me;        
    } else {
        me = std::make_shared<BacktestExecutor>();
        log_set_time_source([melk = std::weak_ptr(me)]{
            auto lk = melk.lock();
            if (lk) return lk->now();
            else return std::chrono::system_clock::now();
        });
        _current_worker = me;
        return me;
    }
}



void BacktestExecutor::set_time(std::chrono::system_clock::time_point tp) {
    auto r = _scheduler.advance_time_until(tp);
    while (r) {
        r.lazy_resume();        
        flush_queue();

        r = _scheduler.advance_time_until(tp);
    }
}

void BacktestExecutor::resume(std::coroutine_handle<> h) noexcept {
    _dispatch_queue.push(h);
}
void BacktestExecutor::resume_idle(std::coroutine_handle<> h) noexcept {
    _idle_queue.push(h);
}

PExecutionWorker BacktestExecutor::spawn() noexcept {
    return shared_from_this();   //just clone self

}
std::chrono::system_clock::time_point BacktestExecutor::now() const {
    return _scheduler.get_current_time();
}
awaitable<bool> BacktestExecutor::sleep_until(std::chrono::system_clock::time_point time_point, cancel_signal *cancel_signal_ptr) {
    return _scheduler.sleep_until(time_point, cancel_signal_ptr);
}
awaitable<bool> BacktestExecutor::sleep_for(std::chrono::system_clock::duration duration, cancel_signal *cancel_signal_ptr) {
    return _scheduler.sleep_for(duration, cancel_signal_ptr);
}
bool BacktestExecutor::cancel(coro::cancel_signal *cancel_signal) {
    if (!cancel_signal) return false;
    bool ok = false;
    while(true) {
        auto r = _scheduler.cancel(cancel_signal);
        if (r) {
            resume(r.release());
            ok = true;
        } else {
            return ok;
        }
    }
}

bool BacktestExecutor::quiesce() {
    if (_dispatch_queue.empty()) return false;
    flush_queue();
    return true;
}

bool BacktestExecutor::empty() const  {
    return _dispatch_queue.empty() && _idle_queue.empty();
}

}