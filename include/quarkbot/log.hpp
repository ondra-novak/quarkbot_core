#pragma once


#include "types.hpp"
#include <format>
#include <source_location>
#include <sstream>
#include <type_traits>


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

    template<>
    inline constexpr auto string_lookup<LogLevel> = make_string_lookup_table<LogLevel>({        
            {LogLevel::disabled,"disabled"},
            {LogLevel::fatal,"fatal"},
            {LogLevel::error,"error"},
            {LogLevel::warning,"warning"},
            {LogLevel::info,"info"},
            {LogLevel::debug,"debug"},
            {LogLevel::trace,"trace"},        
    });

    ///logger object (singleton)
    struct Logger {

        struct Location {
            ///context (part in [])
            std::string_view context;
            ///file path
            std::string_view file;
            ///function name
            std::string_view function;
            ///line number
            uint_least32_t line;
        };



        static constexpr std::pair<std::string_view, std::string_view> crack_function_name(std::string_view name) {
            
            auto find_skip_template = [](std::string_view name, char c) {
                std::size_t pos = 0;
                int level = 0;
                for (auto x: name) {
                    if (x == '<') level++;
                    else if (x == '>') level--;
                    else if (x == c && level == 0) return pos;
                    ++pos;
                }
                pos = name.size();
                return pos;
            };

            auto rfind_skip_template = [](std::string_view name, char c) {
                std::size_t pos = 0;
                int level = 0;
                std::size_t found = name.npos;
                for (auto x: name) {
                    if (x == '<') level++;
                    else if (x == '>') level--;
                    else if (x == c && level == 0) found = pos;;
                    ++pos;
                }
                return found;
            };

            auto pos = find_skip_template(name, '(');            
            if (name.substr(pos).starts_with("(anonymous namespace)::")) {
                name = name.substr(pos+23);
                pos = find_skip_template(name, '(');            
            }
            auto fnonly = name.substr(0,pos);
            if (fnonly.ends_with("operator ")) {
                fnonly = name.substr(0,pos-11);
            }
            auto beg_fn = rfind_skip_template(fnonly, ' ');    
            auto nameonly = fnonly.substr(beg_fn+1);
            if (nameonly.ends_with(">")) {
                auto b = nameonly.find('<');
                if (b && b != nameonly.npos) {
                    nameonly = nameonly.substr(0,b);
                }
            }
            if (nameonly.ends_with("::")) nameonly.remove_suffix(2);
            auto nssep = nameonly.rfind("::");
            auto ns = nssep == nameonly.npos?std::string_view():nameonly.substr(0,nssep);
            auto fnn = nssep == nameonly.npos?nameonly:nameonly.substr(nssep+2);
            return {ns, fnn};
        }


        static Location from(const std::source_location &loc) {
            auto [ctx, fn] = crack_function_name(loc.function_name());
            return {
                ctx,loc.file_name(),fn,loc.line()
            };
        }

        ///current log level
        LogLevel cur_level = LogLevel::disabled;
        ///log sink - who processing logs
        /**
        @param level level of this log message. Current level is not tested, everything is logged
        @param location source_location refering to source location
        @param line conten to the log line

        @note default sink is empty, so no log is produced. See redirection functions
        */
        void (*log_sink)(LogLevel level, const Location &location, std::string_view content)
             = [](LogLevel , const Location &,  std::string_view ) {/*no logger by default*/};        
    


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
        if (Logger::instance.cur_level < level) return;        
        char buffer[1024];
        auto res = std::format_to_n<char *, typename LoggerTypeType<Args>::type...>(
                    buffer, sizeof(buffer), format,  LoggerTypeType<Args>()(std::forward<Args>(args))...);
        Logger::instance.log_sink(level, Logger::from(format._loc), {buffer, static_cast<std::size_t>(res.size)});
        
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


    ///Log output with callback returning the content
    template<std::invocable<> CB>
    requires (std::is_convertible_v<std::invoke_result_t<CB>, std::pair<Logger::Location, std::string_view> >)
    inline void logOutputCB(LogLevel level, CB callback) {
        if (Logger::instance.cur_level < level) return;        
        auto [location, line] = callback();
        Logger::instance.log_sink(level, location, std::string_view(line));
    }


    template<typename T>
    auto logStreamedItem(const T &val) {
        return [val] {
            std::ostringstream s;
            s << val;
            return std::move(s).str();
        };
    }
    

    
}