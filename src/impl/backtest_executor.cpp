#include "backtest_executor.hpp"
#include "ifc/log.hpp"
#include "ifc/strategy_fragment.hpp"
#include "impl/logger.hpp"

#include <chrono>
#include <coroutine>
#include <format>
#include <iterator>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <unordered_map>

namespace quarkbot {

class CoroRegister : public IAsyncDebugTrace {
public:

        virtual void add(std::coroutine_handle<> h, const std::source_location &loc) {
            _loc[h.address()] = loc;
        }
        virtual void remove(std::coroutine_handle<> h) {
            report("Finished", h);
            _loc.erase(h.address());
        }

        void report(std::string_view operation, std::coroutine_handle<> h) {
            auto iter = _loc.find(h.address());
            if  (iter == _loc.end()) return;
            auto &buff = Logger::instance.get_buffer();
            std::format_to(std::back_inserter(buff), "{}: {}", operation, iter->second.function_name());
            Logger::instance.log_sink(LogLevel::trace, nullptr, {buff.begin(), buff.end()});
            buff.clear();

        }

        virtual void resumed(std::coroutine_handle<> h) {
            report("Running", h);
        }

        const std::source_location *find(std::coroutine_handle<> h) const {
            auto iter = _loc.find(h.address());
            if  (iter == _loc.end()) return nullptr;
            else return &iter->second;
        }


protected:
    std::unordered_map<void *, std::source_location> _loc;
};

static CoroRegister coroRegister;


class BacktestExecutorFactory: public BacktestExecutor {
public:
    BacktestExecutorFactory() = default;
};


void BacktestExecutor::flush_queue() {
    while (!_dispatch_queue.empty()) {
        _dispatch_queue.pop();
    }
}

std::shared_ptr<BacktestExecutor> BacktestExecutor::create() {
    auto cur = IExecutionWorker::current();
    auto me = std::dynamic_pointer_cast<BacktestExecutor>(cur);
    if (me) {
        if (cur) throw std::runtime_error("Thread is alread execution worker of different type");
        return me;        
    } else {
        me = std::make_shared<BacktestExecutorFactory>();
        log_set_time_source([melk = std::weak_ptr(me)]{
            auto lk = melk.lock();
            if (lk) return lk->now();
            else return std::chrono::system_clock::now();
        });
        _current_worker = me;
        if (Logger::instance.cur_level <= LogLevel::trace) {
            IAsyncDebugTrace::trace = &coroRegister;
        }
        return me;
    }
}


void BacktestExecutor::set_initial_time(std::chrono::system_clock::time_point tp) {
    auto r = _scheduler.advance_time_until(tp);
    while (r) {
        r.lazy_resume();        
        r = _scheduler.advance_time_until(tp);
    }

}

void BacktestExecutor::set_time(std::chrono::system_clock::time_point tp) {
    flush_queue();
    set_initial_time(tp);
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

bool BacktestExecutor::empty() const  {
    return _dispatch_queue.empty();
}

}