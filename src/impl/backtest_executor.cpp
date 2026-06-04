#include "backtest_executor.hpp"

#include <memory>
#include <stdexcept>

namespace quarkbot {


void BacktestExecutor::flush_queue() {
    while (!_dispatch_queue.empty()) {
        _dispatch_queue.pop();
    }
}

std::shared_ptr<BacktestExecutor> BacktestExecutor::create() {
    auto cur = IExecutionWorker::current();
    auto me = std::dynamic_pointer_cast<BacktestExecutor>(cur);
    if (!me) {
        if (cur) throw std::runtime_error("Thread is alread execution worker of different type");
        auto me = std::shared_ptr<BacktestExecutor>();
        _current_worker = me;
    }
    return me;
}


void BacktestExecutor::set_time(std::chrono::system_clock::time_point tp) {
    flush_queue();
    auto r = _scheduler.advance_time_until(tp);
    while (r) {
        r.lazy_resume();        
        r = _scheduler.advance_time_until(tp);
    }
    flush_queue();
}

void BacktestExecutor::resume(std::coroutine_handle<> h) noexcept {
    _dispatch_queue.push(h);
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

void BacktestExecutor::join() {
    flush_queue();
}

}