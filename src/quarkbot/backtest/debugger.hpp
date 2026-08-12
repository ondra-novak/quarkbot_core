#pragma  once

#include "ibacktest_debugger.hpp"
#include <condition_variable>
#include <mutex>
namespace quarkbot {


class BasicDebuggerImpl final: public IBacktestDebugger {
public:

    virtual bool on_debugger_event(const Timestamp &tp, EventType type) override;
    virtual void on_exit() override;
    virtual Status get_status() const override;
    virtual void set_running(bool running) override;
    virtual void set_trace_mode(TraceMode mode) override;
    virtual void set_breakpoint(Timestamp tp) override;
    virtual void quit() override;


protected:
    mutable std::mutex _mx;
    std::condition_variable _cv;
    bool _quit = false;
    
    Status status = {RunStatus::paused,EventType::data,{},{},{}};


};

}