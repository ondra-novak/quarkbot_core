#include "backtest_worker.hpp"
#include "coro/src/basic_coro/prepared_coro.hpp"
#include "ifc/defs.hpp"
#include <mutex>

namespace quarkbot {
 
std::chrono::system_clock::time_point BacktestExecutionWorker::now() const {
    return _cur_time.load();
}

void BacktestExecutionWorker::resume(std::coroutine_handle<> h) noexcept {
    std::lock_guard _(_mx);
    _microtask_queue.push(h);
}

awaitable<void> BacktestExecutionWorker::sleep_until(std::chrono::system_clock::time_point time_point, alert_flag *alert_flag_ptr) {
    return [&](awaitable<void>::result prom) mutable {
        std::lock_guard _(_mx);
        if (alert_flag_ptr && *alert_flag_ptr) return prom.set_empty();
        _sch_queue.push(IExecutionWorker::proxy_result(std::move(prom)), time_point, alert_flag_ptr);
        return coro::prepared_coro{};
    };

}
awaitable<void> BacktestExecutionWorker::sleep_for(std::chrono::system_clock::duration duration, alert_flag *alert_flag_ptr) {
    return sleep_until(_cur_time.load()+duration, alert_flag_ptr);
}
void BacktestExecutionWorker::interrupt(coro::alert_flag *alert_flag) {
    std::lock_guard _(_mx);
    auto iter = _sch_queue.find(alert_flag);
    if (alert_flag) alert_flag->set();
    if (iter != _sch_queue.end()) {
        resume(_sch_queue.mutable_ref(iter)->value());
        _sch_queue.erase(iter);
    } 
}
PExecutionWorker BacktestExecutionWorker::spawn() noexcept {
    return shared_from_this();

}

void BacktestExecutionWorker::set_current_time(std::chrono::system_clock::time_point tp) {    
    IExecutionWorker::_current_worker = weak_from_this();
    while (true) {
        std::unique_lock lk(_mx);
        while (!_microtask_queue.empty()) {
            auto h = std::move(_microtask_queue.front());
            _microtask_queue.pop();
            lk.unlock();
            h.resume();
            lk.lock();
        }

        if (_sch_queue.empty() || _sch_queue.top().time > tp) {
            _cur_time = tp;
            return;
        } else {
            auto &itm = _sch_queue.top();
            auto p = std::move(itm.value);
            _cur_time = itm.time;
            _sch_queue.pop();
            lk.unlock();
            p();
        }
    }
}
    


}