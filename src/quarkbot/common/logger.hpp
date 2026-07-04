#pragma  once
#include "quarkbot/log.hpp"
#include <chrono>
#include <filesystem>
#include <functional>

namespace quarkbot {

    ///redirect log to stderr
    /**
        All logs are sent to stderr
        */
    void log_to_stderr();
    ///redirect log to a file
    /**
    @param file target file
        */
    void log_to_file(const std::filesystem::path &file);        
    ///redirect log to a file and perform autorotate
    /**
    @param file target file
    @param retention how many files are kept before they are deleted
    @param rotate_seconds how often are files rotated
        */
    void log_to_file_rotate(const std::filesystem::path &file,
             unsigned int retention = 7, unsigned int rotate_seconds = 24*60*60);        

    void log_set_level(LogLevel level);
    ///close any logging
    void log_close();

    void log_set_time_source(std::function<std::chrono::system_clock::time_point()> time_source);
}