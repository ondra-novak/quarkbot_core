#include "logger.hpp"
#include <quarkbot/log.hpp>
#include <quarkbot/utils/string_utils.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <ranges>
#include <source_location>
#include <string_view>
#include <system_error>

namespace quarkbot {


namespace {


    std::mutex _mx;
    std::filesystem::path _cur_log_file = {};
    std::ofstream _cur_log_stream = {};
    std::atomic<bool> _rotate_signal = {};    
    std::size_t _rotate_number = 0;
    std::size_t _rotation_interval = 0;
    std::size_t _rotation_retention = 0;
    std::vector<char> _format_buffer;
    std::function<std::chrono::system_clock::time_point()> _time_source = []{return std::chrono::system_clock::now();};


    void open_file_by_rotate_number(std::size_t rotnum) {
        _rotate_number = rotnum;
        auto stem = _cur_log_file.stem().string();
        time_t tim = static_cast<time_t>(rotnum * _rotation_interval);                
        auto new_name = std::format("{}_{:%Y%m%d %H%M%S}{}", stem,std::chrono::system_clock::from_time_t(tim), _cur_log_file.extension().string());
        auto dir = _cur_log_file.parent_path();
        auto new_file_name = dir/new_name;

        std::error_code ec;

        std::vector<std::filesystem::path> cur_list;
        for(const auto &entry: std::ranges::subrange(std::filesystem::directory_iterator(dir, ec), std::filesystem::directory_iterator())) {
            if (entry.is_regular_file()) {
                auto cstem = entry.path().stem().string();
                if (stem.starts_with(cstem)) {
                    cur_list.push_back(entry.path());
                }
            }
        }
        auto cnt = cur_list.size();
        if (_rotation_retention || cnt > _rotation_retention) {
            std::sort(cur_list.begin(), cur_list.end());
            for (const auto &p: cur_list) {
                std::filesystem::remove(p,ec);
                --cnt;
                if (cnt <= _rotation_retention) break;
            }
        }
        _cur_log_stream.clear();
        _cur_log_stream.open(new_name, std::ios::app|std::ios::out);
        std::filesystem::remove(_cur_log_file, ec);
        std::filesystem::create_symlink(new_file_name,_cur_log_file, ec);
    }

    struct thread_id_counter {
        std::size_t cur_id;
        static std::atomic<std::size_t> next_id;
        thread_id_counter() :cur_id(next_id.fetch_add(1, std::memory_order_relaxed)) {}        
    };

    std::atomic<std::size_t> thread_id_counter::next_id = {};
    thread_local thread_id_counter thread_counter = {};

    constexpr std::array<std::string_view, 6> level_names({
        "FATAL",
        "ERROR",
        "Warn.",
        "Info.",
        "debug",
        "trace"
    });

    std::string_view class_name(const Logger::Location &loc) {
        std::string_view fn = loc.function;
        unsigned int br = 0;
        std::size_t last_spc = std::string_view::npos;
        std::size_t idx =0;
        for (auto &x: fn) {
            if (x == '<') br++;
            if (x == '>') br--;
            if (!br) {
                if (x == ' ') last_spc =  idx;
                if (x == '(') break;
            }
            ++idx;
        }
        if (br) return "?";
        std::string_view part = fn.substr(0,idx).substr(last_spc+1);
        auto sep = part.rfind("::");
        if (sep == part.npos) return "-";
        return part.substr(0,sep);       
    }

    void file_sink_lk(std::chrono::system_clock::time_point tp, std::ostream &file, LogLevel level, const Logger::Location &location, std::string_view content) {
         auto now_mseconds = std::chrono::time_point_cast<std::chrono::milliseconds>(tp);
         auto ins = std::back_inserter(_format_buffer);
         std::format_to(ins, "{:%Y-%m-%d %H:%M:%S} {}", now_mseconds, level_names[static_cast<std::size_t>(level)]);
         if (thread_counter.cur_id) std::format_to(ins, " T{}", thread_counter.cur_id);         
         std::format_to(ins, " [{}]", class_name(location));
         *ins++ = ' ';
         ins = std::transform(content.begin(), content.end(), ins, [](char c){
            return c == '\n'?'\x7F':c;
         });
         {
            std::string_view filename = location.file;
            auto sep = filename.find_last_of("\\/");
            if (sep != filename.npos) filename.remove_prefix(sep+1);
            std::format_to(ins, " {{{}:{}}}", filename, location.line);         
         }
         *ins++ = '\n';
        file.write(_format_buffer.data(),static_cast<int>( _format_buffer.size()));
        _format_buffer.clear();
    }

    void std_error_sink(LogLevel level, const Logger::Location &location, std::string_view content) {
        std::scoped_lock _(_mx);
        file_sink_lk(_time_source(), std::cerr, level, location, content);
    }
    void std_file_sink(LogLevel level, const Logger::Location &location, std::string_view content) {
        std::scoped_lock _(_mx);
        if (_rotate_signal) {
            _cur_log_stream.close();
            _cur_log_stream.clear();
            _rotate_signal.store(false, std::memory_order_relaxed);
            _cur_log_stream.open(_cur_log_file, std::ios::out|std::ios::app);
        }
        file_sink_lk(_time_source(), _cur_log_stream, level, location, content);
    }
    void std_file_rotate_sink(LogLevel level, const Logger::Location &location, std::string_view content) { 
        std::scoped_lock _(_mx);
        auto now =std::chrono::system_clock::now();        
        auto rotnum = static_cast<std::size_t>(std::chrono::system_clock::to_time_t(now))/_rotation_interval;
        if (_rotate_number != rotnum) {
            _cur_log_stream.close();        
            open_file_by_rotate_number(rotnum);
        }
        file_sink_lk(_time_source(), _cur_log_stream, level, location, content);
    }
}

void log_to_stderr() {
    std::scoped_lock _(_mx);
    Logger::instance.log_sink = std_error_sink;
}
void log_to_file(const std::filesystem::path &file) {
    std::scoped_lock _(_mx);
    _cur_log_file = file;
    _cur_log_stream.clear();
    _cur_log_stream.open(_cur_log_file, std::ios::out|std::ios::app);
    Logger::instance.log_sink = std_file_sink;
    signal(SIGUSR1, [](int){
        _rotate_signal.store(true, std::memory_order_relaxed);
    });
}
void log_to_file_rotate(const std::filesystem::path &file, unsigned int retention, unsigned int rotate_seconds) {
    std::scoped_lock _(_mx);
    if (rotate_seconds == 0){
         log_to_file(file);
    } else {
        _rotation_interval = rotate_seconds;
        _rotation_retention = retention;
        _cur_log_file = file;
        auto now =std::chrono::system_clock::now();        
        auto rotnum = static_cast<std::size_t>(std::chrono::system_clock::to_time_t(now))/_rotation_interval;
        open_file_by_rotate_number(rotnum);
        Logger::instance.log_sink = std_file_rotate_sink;
    }

}
void log_set_level(LogLevel level) {
    Logger::instance.cur_level = level;
}

void log_close() {
    Logger df;
    Logger::instance = df;
    _cur_log_stream.close();
    _cur_log_stream.clear();
}

void log_set_time_source(std::function<std::chrono::system_clock::time_point()> time_source) {
    _time_source = time_source;
}

}
