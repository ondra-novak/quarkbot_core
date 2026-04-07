#include "backtest_executor.hpp"

namespace quarkbot {


void BacktestExecutor::flush_queue() {
    while (!_dispatch_queue.empty()) {
        _dispatch_queue.pop();
    }
}

void BacktestExecutor::attach_to_thread() {
    _current_worker = weak_from_this();;
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
void BacktestExecutor::cancel(coro::cancel_signal *cancel_signal) {
    auto r = _scheduler.cancel(cancel_signal);
    resume(r.symmetric_transfer());
}


}