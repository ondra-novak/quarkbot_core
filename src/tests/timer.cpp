#include "check.h"
#include "ifc/defs.hpp"
#include "ifc/timer.hpp"
#include "ifc/scope_counter.hpp"
#include "ifc/strategy_fragment.hpp"
#include "impl/thread_executor.hpp"
#include <chrono>
#include <mutex>

using namespace quarkbot;



class TestTimerObject {
public:


    StrategyFragment run_in_cycle() {        
        std::lock_guard _(_exit_notify);
        while (co_await _timer.sleep_for(std::chrono::milliseconds(10))) {
            _counter++;
        }
    }

    unsigned int stop_join_and_get_counter() {
        _timer.cancel();
        _exit_notify.join();    //wait until task is done
        return _counter;
    }

protected:
    Timer _timer;
    ScopeCounter _exit_notify;
    unsigned int _counter = 0;
};

int main() {
    ExecutionWorker worker( ThreadExecutor::create());
    TestTimerObject obj;
    worker.run(obj.run_in_cycle());
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto count = obj.stop_join_and_get_counter();
    CHECK_BETWEEN(90, count, 110);
}