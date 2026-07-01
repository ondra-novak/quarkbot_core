#pragma once

#include "socket.hpp"
#include "sslobjects.hpp"
#include "stream_concept.hpp"
#include <array>
#include <chrono>
#include <mutex>



namespace network {



class SSLSocketStream {
public:
    
    SSLSocketStream(const PSSL_CTX &ctx, Socket socket);

    bool accept();
    bool connect(const std::string &host);

    std::string_view read();
    bool write(std::string_view data);
    void put_back(std::string_view data);
    void set_read_timeout(std::chrono::milliseconds timeout);
    void set_write_timeout(std::chrono::milliseconds timeout);
    void close();

protected:
    std::array<char, 17000> _input_buffer = {};  //16384 is maximum websocket frame size, but we need some extra space for headers and fragmentation
    std::string_view _unprocessed = {};
    std::chrono::milliseconds _read_timeout = std::chrono::milliseconds(10000);
    std::chrono::milliseconds _write_timeout = std::chrono::milliseconds(1000);
    PSSL _ssl;
    Socket _socket;
    std::mutex _sslmx;
    bool _eof = false;
    bool _closed = false;

    enum class State {
        ready,
        timeout,
        eof
    };

    State handle_ssl_error(std::unique_lock<std::mutex> &lk, int st);
};

StreamWrapper<SSLSocketStream> connect(const PSSL_CTX &ctx, const std::string &host, std::uint16_t port, 
            std::chrono::milliseconds connect_timeout = std::chrono::milliseconds(10000));

}