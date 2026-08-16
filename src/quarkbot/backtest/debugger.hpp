#pragma  once

#include "ibacktest_debugger.hpp"
#include "quarkbot/exchange.hpp"
#include <condition_variable>
#include <mutex>
namespace quarkbot {


class BasicDebuggerImpl final: public IBacktestDebugger {
public:

    BasicDebuggerImpl(Exchange exchange):_exchange(std::move(exchange)) {}

    virtual bool on_debugger_event(const Timestamp &tp, EventType type) override;
    virtual void on_exit() override;
    virtual Status get_status() const override;
    virtual void set_running(bool running) override;
    virtual void set_trace_mode(TraceMode mode) override;
    virtual void set_breakpoint(Timestamp tp) override;
    virtual void quit() override;
    virtual Exchange get_exchange() const override ;


protected:
    mutable std::mutex _mx;
    std::condition_variable _cv;
    bool _quit = false;
    Exchange _exchange;
    
    Status status = {RunStatus::running,EventType::data,{},TraceMode::step,{}};


};

}