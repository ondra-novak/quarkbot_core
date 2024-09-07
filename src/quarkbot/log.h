#pragma once

#include "output_formatter.h"
#include "wrapper.h"
#include <iterator>

#include <vector>
namespace quarkbot {

class ILog {
public:

    enum class Serverity {
        trace = 0,
        debug = 1,
        info = 2,
        warning = 3,
        error = 4,
        fatal = 5,
        disabled = 100
    };

    virtual void output(Serverity level, std::string_view msg) const = 0;
    virtual Serverity get_min_level() const = 0;
    virtual ~ILog() = default;

    class Null;
};

inline std::string_view to_string(ILog::Serverity srvt) {
    switch (srvt) {
        case ILog::Serverity::debug: return "debug";
        case ILog::Serverity::trace: return "trace";
        case ILog::Serverity::info: return "info";
        case ILog::Serverity::warning: return "warning";
        case ILog::Serverity::error: return "ERROR";
        case ILog::Serverity::fatal: return "FATAL";
        case ILog::Serverity::disabled: return "disabled";
        default: return "unknown";
    }
}

class ILog::Null: public ILog {
public:
    virtual void output(Serverity , std::string_view ) const override {}
    virtual Serverity get_min_level() const override {return Serverity::disabled;}
};



class Log  : public Wrapper<ILog>{
public:



    using Serverity = ILog::Serverity;

    Log() = default;
    Log(std::shared_ptr<const ILog> lgptr):Wrapper<ILog>(lgptr),_min_level(this->_ptr->get_min_level()) {}

    ///Create new log object with context
    /**
     * @param other source log object
     * @param pattern pattern to format context
     * @param args arguments to format context
     *
     * @note context is copied to every log line in square brackets
     *      [context1][context2][context3] log line
     */
    template<typename ... Args>
    Log(const Log &other, std::string_view pattern, Args && ... args)
        :Wrapper<ILog>(other)
        ,_fmt(other._fmt)
        ,_min_level(other._min_level)
    {
        if (_min_level != Serverity::disabled) {
            append_context(pattern, std::forward<Args>(args)...);
        }
    }

    template<typename ... Args>
    Log(Log &&other, std::string_view pattern, Args && ... args)
        :Wrapper<ILog>(std::move(other))
        ,_fmt(std::move(other._fmt))
        ,_min_level(other._min_level)
    {
        if (_min_level != Serverity::disabled) {
            append_context(pattern, std::forward<Args>(args)...);
        }
    }

    template<typename ... Args>
    void output(Serverity level, const std::string_view &pattern, Args && ... args) {
        if (level >= _min_level) {
            _fmt.format(pattern, std::forward<Args>(args)...);
            _ptr->output(level, _fmt.get_buffer());
            _fmt.resize_buffer(_context_size);
        }
    }

    template<typename ... Args>
    void trace(const std::string_view &pattern, Args && ... args) {
        output(Serverity::trace, pattern, std::forward<Args>(args)...);
    }
    template<typename ... Args>
    void debug(const std::string_view &pattern, Args && ... args) {
        output(Serverity::debug, pattern, std::forward<Args>(args)...);
    }
    template<typename ... Args>
    void info(const std::string_view &pattern, Args && ... args) {
        output(Serverity::info, pattern, std::forward<Args>(args)...);
    }
    template<typename ... Args>
    void warning(const std::string_view &pattern, Args && ... args) {
        output(Serverity::warning, pattern, std::forward<Args>(args)...);
    }
    template<typename ... Args>
    void error(const std::string_view &pattern, Args && ... args) {
        output(Serverity::error, pattern, std::forward<Args>(args)...);
    }
    template<typename ... Args>
    void fatal(const std::string_view &pattern, Args && ... args) {
        output(Serverity::fatal, pattern, std::forward<Args>(args)...);
    }

protected:
    OutpuFormatter _fmt;
    Serverity _min_level;
    std::size_t _context_size = 0;

    template<typename ... Args>
    void append_context(const std::string_view &pattern, Args && ... args) {
        _fmt.push_back('[');
        _fmt.format(pattern, std::forward<Args>(args)...);
        _fmt.push_back(']');
        _context_size = _fmt.size();
    }


};





}
