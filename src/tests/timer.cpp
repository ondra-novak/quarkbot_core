#include "ifc/timer.hpp"
#include "check.h"
#include "ifc/defs.hpp"
#include "ifc/strategy_fragment.hpp"
#include "impl/thread_executor.hpp"
#include <chrono>

using namespace quarkbot;



class TestTimerObject {
public:

    TestTimerObject(PExecutionWorker worker):_timer(worker) {};

    StrategyFragment run_in_cycle() {
        while (co_await _timer.sleep_for(std::chrono::milliseconds(10))) {
            _counter++;
        }
        _timer.notify_exit();
    }

    unsigned int stop_join_and_get_counter() {
        _timer.cancel_and_join();
        return _counter;
    }

protected:
    Timer _timer;
    unsigned int _counter = 0;
};



int main() {
    auto worker = ThreadExecutor::create();
    TestTimerObject obj(worker);
    worker->run(obj.run_in_cycle());
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto count = obj.stop_join_and_get_counter();
    CHECK_BETWEEN(90, count, 110);
}