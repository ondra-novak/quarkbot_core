#pragma once

#include "libs/network/sslobjects.hpp"
#include <chrono>
#include <concepts>
#include <cstdint>
#include <string_view>
namespace network {



class Socket {
public:
    Socket(int socket): _socket(socket) {}
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&other): _socket(other._socket) {
        other._socket = -1;
    }
    Socket &operator=(Socket &&other) {
        if (this != &other) {
            close();
            _socket = other._socket;
            other._socket = -1;
        }
        return *this;
    }
    ~Socket() {
        close();
    }
    void close() ;
    bool wait_read(int timeoutms);
    bool wait_write(int timeoutms);
    operator int() const {return _socket;}
    static Socket connect(const std::string &host, std::uint16_t port, std::chrono::milliseconds connect_timeout = std::chrono::milliseconds(30000));
protected:
    int _socket; 

};
}