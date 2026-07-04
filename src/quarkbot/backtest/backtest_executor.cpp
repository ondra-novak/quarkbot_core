#include "backtest_executor.hpp"

#include "../common/logger.hpp"

#include "quarkbot/log.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/utils/string_utils.hpp"

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
            report("Fragment created:", h);
        }
        virtual void remove(std::coroutine_handle<> h) {
            report("Fragment finished: ", h);
            _loc.erase(h.address());
        }

        void report(std::string_view operation, std::coroutine_handle<> h) {
            auto iter = _loc.find(h.address());
            if  (iter == _loc.end()) return;
            auto &buff = Logger::get_buffer();
            std::string_view fnam(iter->second.function_name())            ;            
            std::format_to(std::back_inserter(buff),"{} {}", operation, short_name(fnam) );
            Logger::instance.log_sink(LogLevel::trace, &iter->second, {buff.data(), buff.size()});
            buff.clear();

        }

        static std::string_view short_name(std::string_view n){
            auto sz = n.size();
            int bc = 0;
            for (auto i = sz-sz; i < sz; ++i) {
                char c = n[i];
                if (isspace(c) && bc == 0) {
                    n = n.substr(i);
                    break;
                }
                if (c == '<') bc++;
                if (c == '>') bc--;                
            }
            auto rc = n.find('(');
            if (rc != n.npos) n = n.substr(0,rc);
            n = trim(n);
            return n;
        }

        virtual void resumed(std::coroutine_handle<> h) {
            report("Fragment running", h);
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
        auto p = std::move(_dispatch_queue.front());
        _dispatch_queue.pop();
        p.lazy_resume();
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
        if (Logger::instance.cur_level >= LogLevel::trace) {
            IAsyncDebugTrace::trace = &coroRegister;
        }
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

bool BacktestExecutor::join() {
    if (_dispatch_queue.empty()) return false;
    flush_queue();
    return true;
}

bool BacktestExecutor::empty() const  {
    return _dispatch_queue.empty();
}

}