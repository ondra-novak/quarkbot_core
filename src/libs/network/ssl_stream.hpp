#pragma once

#include "libs/network/socket.hpp"
#include "libs/network/sslobjects.hpp"
#include "stream_concept.hpp"
#include <array>
#include <chrono>
#include <mutex>



namespace network {



class SSLSocketStream {
public:
    
    SSLSocketStream(PSSL_CTX ctx, Socket socket);

    bool accept();
    bool connect();

    std::string_view read();
    bool write(std::string_view data);
    void put_back(std::string_view data);
    void set_read_timeout(std::chrono::milliseconds timeout);
    void set_write_timeout(std::chrono::milliseconds timeout);
    void close();

protected:
    std::array<char, 17000> _input_buffer;  //16384 is maximum websocket frame size, but we need some extra space for headers and fragmentation
    std::string_view _unprocessed;
    std::chrono::milliseconds _read_timeout = std::chrono::milliseconds(30000); //default read timeout is 30 seconds, it should be enough for most use cases, and also allows to detect dead connections
    std::chrono::milliseconds _write_timeout = std::chrono::milliseconds(30000); //default write timeout is 30 seconds, it should be enough for most use cases, and also allows to detect dead connections
    PSSL _ssl;
    Socket _socket;
    std::mutex _wrmx;
    std::mutex _sslmx;
    bool _eof;
    bool _closed;

    enum class State {
        ready,
        timeout,
        eof
    };

    State handle_ssl_error(std::unique_lock<std::mutex> &lk, int st);
};



}