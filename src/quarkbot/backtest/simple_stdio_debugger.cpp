#include "simple_stdio_debugger.hpp"
#include "ibacktest_debugger.hpp"
#include "persistent_reporter.hpp"
#include "../common/deserialize_resolver.hpp"
#include "quarkbot/json/json.hpp"
#include "quarkbot/persistent.hpp"
#include "quarkbot/serializer/deserialize_from_schema.hpp"
#include "quarkbot/serializer/schema_fwd.hpp"
#include "quarkbot/timestamp.hpp"
#include "quarkbot/types.hpp"
#include "quarkbot/storage_srl.hpp"
#include "quarkbot/utils/lookup.hpp"
#include "quarkbot/utils/string_utils.hpp"
#include <charconv>
#include <chrono>
#include <csignal>
#include <ctime>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <thread>

namespace quarkbot {


class SimpleStdioDebugger {
public:
    SimpleStdioDebugger(std::shared_ptr<IBacktestDebugger> control,Storage store)
        :_control(std::move(control)), _store(std::move(store)) {            
            _repl = connect_variable_reporter(_store, std::cout);
        }
        

    void run() {
        _thr  = std::jthread([this](std::stop_token tkn){
            worker(tkn);
        });
    }


protected:
    std::jthread _thr;
    std::shared_ptr<IBacktestDebugger> _control;
    Storage _store;
    Storage::Replicator::Connection _repl;
    Timestamp _now;

    void worker(std::stop_token tkn);

    void help();
    void step();
    void next();
    void quit();
    void cont();
    void skip(std::string_view arg);
    void print(std::string_view arg);
    void prints(std::string_view arg);

    Json value_to_json(const auto &v);

    static constexpr auto command_list = make_lookup_table<std::string_view, void (SimpleStdioDebugger::*)()>({
        {"help", &SimpleStdioDebugger::help},
        {"h", &SimpleStdioDebugger::help},
        {"step", &SimpleStdioDebugger::step},
        {"t", &SimpleStdioDebugger::step},
        {"next", &SimpleStdioDebugger::next},
        {"n", &SimpleStdioDebugger::next},
        {"quit", &SimpleStdioDebugger::quit},
        {"q", &SimpleStdioDebugger::quit},
        {"cont", &SimpleStdioDebugger::cont},
        {"c", &SimpleStdioDebugger::cont},
    });

    static constexpr auto command_list_arg = make_lookup_table<std::string_view, void (SimpleStdioDebugger::*)(std::string_view)>({
        {"skip",&SimpleStdioDebugger::skip},
        {"s",&SimpleStdioDebugger::skip},
        {"print",&SimpleStdioDebugger::print},
        {"p",&SimpleStdioDebugger::print},
        {"prints",&SimpleStdioDebugger::prints},
        {"ps",&SimpleStdioDebugger::prints}
    });

};

std::function<void(std::shared_ptr<IBacktestDebugger>, Storage)> get_simple_stdio_debugger() {
    std::shared_ptr<SimpleStdioDebugger> inst;
    return [inst](std::shared_ptr<IBacktestDebugger> dbg, Storage storage) mutable {
        inst = std::make_shared<SimpleStdioDebugger>(dbg, storage);
        inst->run();
    };
}


static std::atomic<bool> interrupted = {false};


void SimpleStdioDebugger::worker(std::stop_token tkn) {
    signal(SIGINT,[](int){
        interrupted.store(true);
    });
    std::println(std::cout, "Debugger active, type 'help' for help");
    std::string cmdline;
    std::string prev_line;
    while (!tkn.stop_requested()) {
        if (interrupted.exchange(false)) {
            _control->set_running(false);
        }
        auto st = _control->get_status();        
        if (st.run_status == IBacktestDebugger::RunStatus::done) break;
            
            
    
        while (st.run_status == IBacktestDebugger::RunStatus::paused) {
            
            try {shared_transaction({});} catch (...) {} //throws exception, but flushes pending transaction

            _now = st.time;
            std::print(std::cout, "paused  {:%Y-%m-%d %H:%M:%S} > ", st.time);

            if (std::getline(std::cin, cmdline)) {
                if (cmdline.empty()) cmdline = std::move(prev_line);
                if (!cmdline.empty()) {
                    std::string_view cmdlinestr = trim(cmdline);
                    auto c1 = command_list(cmdlinestr);
                    if (c1.has_value()) {
                        (this->*(*c1))();
                    } else {
                        auto cmd = trim(split(cmdlinestr, " "));
                        auto c2 = command_list_arg(cmd);
                        if (c2) {
                            auto params = trim(cmdlinestr);
                            if (params.empty()) {
                                std::println(std::cout,"ERROR: Expected argument for command {}", cmd);
                            } else {
                                (this->*(*c2))(params);
                            }
                        } else {
                            std::println(std::cout, "Unknown command: ");
                        }
                    } 
                    prev_line = std::move(cmdline);
                }               
            } else {
                _control->quit();
            }
            st = _control->get_status();        
        }
                
        std::print(std::cout, "running {:%Y-%m-%d %H:%M:%S}\r", st.time);
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    
    
    signal(SIGINT,SIG_DFL);

    


}

void SimpleStdioDebugger::help(){
    std::puts(""
        "Debugger help\n"
        "-----------------------\n"
        "help (h)                   this help\n"
        "\n"
        "step (t)                   step to next tick\n"
        "\n"
        "next (n)                   step to next event\n"
        "\n"
        "cont (c)                   continue (Ctrl+C break)\n"
        "\n"
        "quit (q)                   quit\n"
        "\n"
        "skip (s) <time/span>       run until time or span is reached\n"
        "                               you can use suffixes 's' for seconds, 'm' for minutes, 'h' for hours, 'd' for days\n"
        "                           time can be in various formats\n"
        "                               full time format: 2023-01-01 12:00:00,\n"
        "                               just time (nearest): 12:00:00 ,\n"
        "                               just time without seconds (nearest): 12:00 ,\n"
        "\n"
        "print <var>                print value of persisted variable\n"
        "\n"
        "prints <var>               print persisted serie\n"
        "\n"
        "<enter>             repeat previous command\n"
    );

}
void SimpleStdioDebugger::step(){
    _control->set_trace_mode(IBacktestDebugger::TraceMode::step);
    _control->set_running(true);
}
void SimpleStdioDebugger::next(){
    _control->set_trace_mode(IBacktestDebugger::TraceMode::step_data_event);
    _control->set_running(true);

}
void SimpleStdioDebugger::quit(){
    _control->quit();
}
void SimpleStdioDebugger::cont(){
    auto st = _control->get_status();
    if (st.trace_mode != IBacktestDebugger::TraceMode::breakpoint || st.breakpoint <= _now) {
        _control->set_trace_mode(IBacktestDebugger::TraceMode::disabled);
    }
    _control->set_running(true);
}

void SimpleStdioDebugger::skip(std::string_view arg) {
    double c;
    auto r = std::from_chars(arg.data(), arg.data()+arg.size(), c);
    if (r.ec != std::errc{}) {
        std::println(std::cout, "Invalid number format {}", arg);
        return;
    }
    if (r.ptr >= arg.data() + arg.size()) {
        std::println(std::cout, "Expected suffix 'h','m','s','d', or date time HH:MM[:SS], or date YYYY-MM-DD: {}", arg);
        return;
    }
    Timestamp brk = _now;
    switch (*r.ptr) {
        case 's': brk = brk + std::chrono::nanoseconds(static_cast<std::int64_t>(c*1'000'000'000LL));break;
        case 'm': brk = brk + std::chrono::nanoseconds(static_cast<std::int64_t>(c*60'000'000'000LL));break;
        case 'h': brk = brk + std::chrono::microseconds(static_cast<std::int64_t>(c*3600'000'000LL));break;
        case 'd': brk = brk + std::chrono::microseconds(static_cast<std::int64_t>(c*86400'000'000LL));break;
        case '-':   {//parse date
            int R = 0,M = 1,D = 1, h =0, m =0, s= 0;
            std::string str ( arg);
            if (std::sscanf(str.c_str(), "%d-%d-%d %d:%d:%d", &R,&M,&D,&h,&m,&s) < 3) {
                std::println(std::cout, "Argument is not valid date: {}", arg);
                return;
            }
            struct std::tm ts = {
                s,m,h,D,M-1,R-1990,0,0,0,0,"UTC"
            };
            brk = std::chrono::system_clock::from_time_t(std::mktime(&ts));
        }break;
        case ':':   {
            int h =0, m =0, s= 0;
            std::string str ( arg);
            if (std::sscanf(str.c_str(), "%d:%d:%d", &h,&m,&s) < 2) {
                std::println(std::cout, "Argument is not valid time: {}", arg);
                return;
            }
            auto bd = interval_lower_bound(_now, std::chrono::days(1));
            bd += std::chrono::seconds(h*3600+m*60+s);
            while (bd <= _now) {
                bd += std::chrono::days(1);
            }
            brk = bd;
        }
        break;
    }
    _control->set_breakpoint(brk);
    _control->set_running(true);

}

Json SimpleStdioDebugger::value_to_json(const auto &v) {
    srl::SchemaHash h;    
    Json rdval;
    if (v.exists) {
        v.extract(h,h);
        auto sch = _store.get_schema(h);
        if (sch.exists) {
            try {
                auto jsch = Json::from_string(sch.data);
                auto arch = srl::string_deserializer(v.data);
                rdval = srl::deserialize_from_schema(jsch, arch, get_desrl_resolver());
            } catch (...) {
                rdval = binary_content(v.data);
            }
        } else {
            rdval = binary_content(v.data);
        }
    }
    return rdval;
}

void SimpleStdioDebugger::print(std::string_view arg) {
    auto v = _store.get(arg);
    Json rdval = value_to_json(v);
    std::println(std::cout, "{}={}", arg, rdval.to_string());
}
void SimpleStdioDebugger::prints(std::string_view arg) {
    std::print(std::cout, "{}",arg);
    char sep = '=';    
    for (const auto &v: _store.select_range(arg, RecordKey::min(), RecordKey::max())) {
            auto j = value_to_json(v);
            std::print(std::cout, "{}{}", sep, j.to_string());
            sep = ',';
    }
    if (sep == '=') {
        std::print(std::cout, "nullptr");
    }
    std::println(std::cout);
}


}