#include "simple_stdio_debugger.hpp"
#include "ibacktest_debugger.hpp"
#include "persistent_reporter.hpp"
#include "../common/deserialize_resolver.hpp"
#include "quarkbot/backtest/var_inspector.hpp"
#include "quarkbot/event_stream.hpp"
#include "quarkbot/json/json.hpp"
#include "quarkbot/persistent.hpp"
#include "quarkbot/serializer/deserialize_from_schema.hpp"
#include "quarkbot/serializer/schema_fwd.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/stream/snapshot.hpp"
#include "quarkbot/stream/ticker.hpp"
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
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <set>
#include <string>
#include <thread>

namespace quarkbot {


class SimpleStdioDebugger {
public:
    SimpleStdioDebugger(std::shared_ptr<IBacktestDebugger> control,Storage store)
        :_control(std::move(control)), _store(std::move(store)) {            
            _inspector.attach_storage(_store);
            _watcher = _store.add_replicator([this](const Storage::ReplicatorEvent &ev) noexcept {
                std::scoped_lock lock(_mx);
                if (!ev.schema_hash) {
                    auto it = _watches.find(ev.key);
                    if (it != _watches.end() && it->second) {
                        _control->set_running(false);
                    }
                }
            });
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
    VariableInspector _inspector;
    Storage::Replicator::Connection _watcher;
    Timestamp _now;
    ///watch variable and if it is watchpoint - if variable changes, debugger will stop
    std::map<std::string,bool, std::less<> > _watches;
    std::map<std::string,EventStream<Ticker>, std::less<> > _tickers;
    std::mutex _mx;
    

    void worker(std::stop_token tkn);

    void help();
    void step();
    void next();
    void quit();
    void cont();
    void skip(std::string_view arg);
    void print(std::string_view arg);
    void prints(std::string_view arg);
    void watch(std::string_view arg);
    void unwatch(std::string_view arg);
    void watchpoint(std::string_view arg);
    void ticker(std::string_view arg);
    void clear_ticker(std::string_view arg);

    void list_variables();


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
        {"vars", &SimpleStdioDebugger::list_variables},
        {"v", &SimpleStdioDebugger::list_variables}
    });

    static constexpr auto command_list_arg = make_lookup_table<std::string_view, void (SimpleStdioDebugger::*)(std::string_view)>({
        {"skip",&SimpleStdioDebugger::skip},
        {"s",&SimpleStdioDebugger::skip},
        {"print",&SimpleStdioDebugger::print},
        {"p",&SimpleStdioDebugger::print},
        {"prints",&SimpleStdioDebugger::prints},
        {"ps",&SimpleStdioDebugger::prints},
        {"watch",&SimpleStdioDebugger::watch},
        {"w",&SimpleStdioDebugger::watch},
        {"unwatch",&SimpleStdioDebugger::unwatch},
        {"uw",&SimpleStdioDebugger::unwatch},
        {"watchpoint",&SimpleStdioDebugger::watchpoint},
        {"wp",&SimpleStdioDebugger::watchpoint},
        {"ticker", &SimpleStdioDebugger::ticker},
        {"tk", &SimpleStdioDebugger::ticker},
        {"remove_ticker", &SimpleStdioDebugger::clear_ticker},
        {"rtk", &SimpleStdioDebugger::clear_ticker},
        
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

void SimpleStdioDebugger::list_variables() {
    std::scoped_lock _(_mx);

    _inspector.flush(); //flush pending updates to the variable reporter
    if (!_tickers.empty()) {        
        std::println(std::cout, "---Tickers ---");
        for (auto &[var, ticker]: _tickers) {
            Ticker tk;
            if (ticker.current(tk)) {
                std::println(std::cout, "{}={}", var, (tk.quote.both_sides()?tk.quote.mid():tk.stats.last_price).to_string());
            }
        }
    }
    if (!_watches.empty()) {
        std::println(std::cout, "---Watches ---");
        for (const auto &[var, watchpoint]: _watches) {
            std::string_view wp = watchpoint ? "*" : " ";
            std::println(std::cout, " {}{}={}", wp, var, _inspector.inspect(var).to_string()    );
        }
    }
    auto vars = _inspector.inspect_all_updated();
    if (!vars.empty()) {
        std::println(std::cout, "---Updated ---");
        for (const auto &[var, val]: vars) {
            if (_watches.find(var) == _watches.end()) {
                std::println(std::cout, "  {} = {}", var, val.to_string());
            }
        }
    }
}

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
            
            
        if (st.run_status == IBacktestDebugger::RunStatus::paused) {
            std::println(std::cout);
            list_variables();
            while (st.run_status == IBacktestDebugger::RunStatus::paused) {
                

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
            _inspector.clear_updated();

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
        "step (t)                   step to next tick\n"
        "next (n)                   step to next event\n"
        "cont (c)                   continue (Ctrl+C break)\n"
        "vars (v)                   list watched and updated variables\n"
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
        "watch <var>                add variable to watch list (removes watchpoint if set)\n"
        "unwatch <var>              remove variable from watch list\n"
        "watchpoint <var>           set watchpoint on variable (debugger will stop when value changes)\n"
        "ticker <instrument>        add ticker to watch list\n"
        "remove_ticker <instrument> rmeove ticker from watch list\n"
        "<enter>                     repeat previous command\n"
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

static time_t custom_timegm(std::tm* tm_ptr) {
#ifdef _WIN32
    return _mkgmtime(tm_ptr); // Windows specifická funkce pro UTC
#else
    return timegm(tm_ptr);    // POSIX standard pro UTC (Linux/macOS)
#endif
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
        case 's': brk = brk + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(static_cast<std::int64_t>(c*1'000'000'000LL)));break;
        case 'm': brk = brk + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(static_cast<std::int64_t>(c*60'000'000'000LL)));break;
        case 'h': brk = brk + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::microseconds(static_cast<std::int64_t>(c*3600'000'000LL)));break;
        case 'd': brk = brk + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::microseconds(static_cast<std::int64_t>(c*86400'000'000LL)));break;
        case '-':   {//parse date
            int R = 0,M = 1,D = 1, h =0, m =0, s= 0;
            std::string str ( arg);
            if (std::sscanf(str.c_str(), "%d-%d-%d %d:%d:%d", &R,&M,&D,&h,&m,&s) < 3) {
                std::println(std::cout, "Argument is not valid date: {}", arg);
                return;
            }
            struct std::tm ts = {};
            ts.tm_sec = s;
            ts.tm_mday = m;
            ts.tm_hour = h;
            ts.tm_mday = D;
            ts.tm_mon = M-1;
            ts.tm_year = R-1900;
            brk = std::chrono::system_clock::from_time_t(custom_timegm(&ts));
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


void SimpleStdioDebugger::print(std::string_view arg) {
    auto v = _inspector.inspect(arg);
    std::println(std::cout, "{}={}", arg, v.to_string());
}
void SimpleStdioDebugger::prints(std::string_view arg) {
    auto v = _inspector.inspect_series  (arg);
    char sep = '=';    
    for (const auto &v: v) {            
            std::print(std::cout, "{}{}", sep, v.to_string());
            sep = ',';
    }
    if (sep == '=') {
        std::print(std::cout, "nullptr");
    }
    std::println(std::cout);
}
void SimpleStdioDebugger::watch(std::string_view arg) {
    std::scoped_lock _(_mx);
    _watches[std::string(arg)] = false;
}
void SimpleStdioDebugger::unwatch(std::string_view arg) {
    std::scoped_lock _(_mx);
    _watches.erase(std::string(arg));

}
void SimpleStdioDebugger::watchpoint(std::string_view arg) {
    std::scoped_lock _(_mx);
    _watches[std::string(arg)] = true;
}

void SimpleStdioDebugger::ticker(std::string_view arg) {
    std::string err;
    for (auto t: std::array<InstrumentType,4>({InstrumentType::spot, InstrumentType::margin, InstrumentType::contract, InstrumentType::inverse_contract})) {
        try {
            auto ex = _control->get_exchange();
            auto instr = ex.create_instrument(arg, t);
            auto stream = instr.subscribe<Ticker>();
            if (stream.is_open()) {
                _tickers[std::string(arg)] = std::move(stream);
            } else {
                std::println(std::cout, "The ticker stream is not available for {}", arg);
            }
            return;
        } catch (const std::exception &e) {
                err = std::format("Failed to create instrument {}:  {}", arg, e.what());
        }
    } 
    std::println(std::cout, "{}", err);

}
void SimpleStdioDebugger::clear_ticker(std::string_view arg) {
    auto iter = _tickers.find(arg);
    if (iter == _tickers.end()) {
        std::println(std::cout, "Ticker `{}` is not subscribed", arg);
    } else {
        _tickers.erase(iter);
    }
}


}