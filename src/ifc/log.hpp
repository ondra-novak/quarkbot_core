#pragma once


#include <format>
#include <source_location>
#include <type_traits>
#include <vector>


namespace quarkbot {


    ///specifies log level
    enum class LogLevel {        
        disabled = 0,
        ///fatal error - probably abort
        fatal = 0,
        ///error - unhandled exception
        error = 1,
        ///warning - probably error or edge case
        warning = 2,
        ///information, progress, etc
        info = 3,
        ///debug information
        debug = 4,
        ///cycle traces, more debug informations
        trace = 5,
    };

    ///logger object (singleton)
    struct Logger {
        ///current log level
        LogLevel cur_level = LogLevel::disabled;
        ///log sink - who processing logs
        /**
        @param level level of this log message. Current level is not tested, everything is logged
        @param location source_location refering to source location
        @param line conten to the log line

        @note default sink is empty, so no log is produced. See redirection functions
        */
        void (*log_sink)(LogLevel level, const std::source_location &location, std::string_view content)
             = [](LogLevel , const std::source_location &,  std::string_view ) {/*no logger by default*/};        
    

        ///retrieves local buffer suitable to hold message during formation
        /**
        MT safe, buffer is per thread
         */
        static std::vector<char> &get_buffer() {
            static thread_local std::vector<char> buffer;
            return buffer;
        }        

        ///Instabce of global logger
        static Logger instance;

    };

    ///logger instance
    inline  Logger Logger::instance = {};
    
    template<typename T>
    concept TypeToString = requires(T val) {
        {val.to_string()}->std::same_as<std::string>;
    };
    
    template<typename T> struct LoggerTypeType {using type = T;  template<typename X> X &&operator()(X &&val) const {return std::forward<X>(val);}};
    template<TypeToString T> struct LoggerTypeType<T> {using type = std::string; std::string operator()(const T &val) const {return val.to_string();}};
    template<std::invocable<> T> struct LoggerTypeType<T> {
        using type = typename  LoggerTypeType<std::invoke_result_t<T> >::type;
        type operator()(const T &val) const {
            LoggerTypeType<type> out;
            return out(val());
        }
    };

    template<typename CharT, typename ... Args>
    class LogFormatBasicString: public std::basic_format_string<CharT, typename LoggerTypeType<Args>::type...> {
    public:
        std::source_location _loc;

        template<typename _Tp, typename Q = std::source_location>
	    requires std::convertible_to<const _Tp&, std::basic_string_view<CharT>>
	    consteval LogFormatBasicString(const _Tp& __s, Q nfo = std::source_location::current()):std::format_string<typename LoggerTypeType<Args>::type...>(__s),_loc(nfo) {}        
    };


    template<typename... _Args>
    using LogFormatString = LogFormatBasicString<char, std::type_identity_t<_Args>...>;

    template<typename ... Args>
    inline void logOutput(LogLevel level, LogFormatString<Args...> format, Args &&... args ) {
        auto &buffer = Logger::get_buffer();
        if (Logger::instance.cur_level < level) return;        
        std::format_to<std::back_insert_iterator<std::vector<char> >, typename LoggerTypeType<Args>::type...>(
                    std::back_inserter(buffer), format,  LoggerTypeType<Args>()(std::forward<Args>(args))...);
        Logger::instance.log_sink(level, format._loc, {buffer.begin(), buffer.end()});
        buffer.clear();
    }

    template<typename ... Args>
    inline void logDebug(LogFormatString<Args...> format, Args &&... args ) {
        logOutput(LogLevel::debug, format, std::forward<Args>(args)...);
    }
    template<typename ... Args>
    inline void logTrace(LogFormatString<Args...> format, Args &&... args ) {
        logOutput(LogLevel::trace, format, std::forward<Args>(args)...);
    }
    template<typename ... Args>
    inline void logInfo(LogFormatString<Args...> format, Args &&... args ) {
        logOutput(LogLevel::info, format, std::forward<Args>(args)...);
    }
    template<typename ... Args>
    inline void logWarning(LogFormatString<Args...> format, Args &&... args ) {
        logOutput(LogLevel::warning, format, std::forward<Args>(args)...);
    }
    template<typename ... Args>
    inline void logError(LogFormatString<Args...> format, Args &&... args ) {
        logOutput(LogLevel::error, format, std::forward<Args>(args)...);
    }
    template<typename ... Args>
    inline void logFatal(LogFormatString<Args...> format, Args &&... args ) {
        logOutput(LogLevel::fatal, format, std::forward<Args>(args)...);
    }
    
}