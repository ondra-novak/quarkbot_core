#pragma once

#include <stdexcept>
#include <string>
namespace network {

class HttpStatusException: public std::runtime_error {
public:
    HttpStatusException(unsigned int code, std::string_view message, std::string_view context = {})
        :std::runtime_error(build_message(code, message))
        ,code(code), message(message), context(context) {
            std::string msg = std::to_string(code);
            msg.push_back(' ');
            msg.append(message);
        }

    unsigned int get_code() const {return code;}
    const std::string &get_message() const {return message;}
    const std::string &get_context() const {return context;}
protected:

    unsigned int code;
    std::string message;
    std::string context;

    static std::string build_message(unsigned int code, std::string_view message) {
        std::string msg = std::to_string(code);
        msg.push_back(' ');
        msg.append(message);
        return msg;
    }

};

}