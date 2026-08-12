#include "debugger.hpp"
#include <mutex>


namespace quarkbot {

bool BasicDebuggerImpl::on_debugger_event(const Timestamp &tp, EventType type) {
    std::unique_lock lk(_mx);
    status.event = type;
    status.time = tp;
    switch (status.trace_mode) {
        case TraceMode::breakpoint: 
            if (tp >= status.breakpoint) status.run_status = RunStatus::paused;
            break;
        case TraceMode::step:
            status.run_status = RunStatus::paused;
            break;
        case TraceMode::step_data_event:
            if (type == EventType::data)
                status.run_status = RunStatus::paused;
            break;
        default:
           break;
    }

    _cv.wait(lk, [this]{
        return status.run_status != RunStatus::paused || _quit;
    }); 
    return !_quit;
}

void BasicDebuggerImpl::on_exit() {
    std::scoped_lock _(_mx);
    status.run_status = RunStatus::done;    
}

BasicDebuggerImpl::Status BasicDebuggerImpl::get_status() const {
    std::scoped_lock _(_mx);
    return status;
}

void BasicDebuggerImpl::set_running(bool running) {
    std::scoped_lock _(_mx);
    switch (status.run_status){
        case RunStatus::running:
            if (!running) status.run_status = RunStatus::paused;
            break;
        case RunStatus::paused:
            if (running) {
                status.run_status = RunStatus::running;
                _cv.notify_one();
            }
            break;
        default:
            break;  
    }

}

void BasicDebuggerImpl::set_trace_mode(TraceMode mode) {
    std::unique_lock lk(_mx);       
    status.trace_mode = mode;
}

void BasicDebuggerImpl::set_breakpoint(Timestamp tp) {
    std::unique_lock lk(_mx);   
    status.trace_mode = TraceMode::breakpoint;
    status.breakpoint = tp;
}
void BasicDebuggerImpl::quit() {
    std::unique_lock lk(_mx);   
    _quit = true;
}

}
