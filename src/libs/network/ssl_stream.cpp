#include "ssl_stream.hpp"
#include "libs/network/socket.hpp"
#include "libs/network/sslobjects.hpp"

#include <chrono>
#include <mutex>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <fcntl.h>

namespace network {

SSLSocketStream::SSLSocketStream(PSSL_CTX ctx, Socket socket) :_socket(std::move(socket)){
    int flags = fcntl(_socket, F_GETFL, 0);
    fcntl(_socket, F_SETFL, flags | O_NONBLOCK);
    _ssl = PSSL(SSL_new(ctx.get()));
    SSL_set_fd(_ssl.get(), _socket);
}

bool SSLSocketStream::accept(){  
    std::unique_lock lk(_sslmx);
    auto st = SSL_accept(_ssl.get());
    while (st != 1) {
        if (handle_ssl_error(lk, st) != State::ready) {
            _eof = true;
            return false;
        }
        st = SSL_accept(_ssl.get());
    }    
    return true;
}
bool SSLSocketStream::connect(){
    std::unique_lock lk(_sslmx);
    auto st = SSL_connect(_ssl.get());
    while (st != 1) {
        if (handle_ssl_error(lk, st) != State::ready) {
            _eof = true;
            return false;
        }
        st = SSL_connect(_ssl.get());
    }    
    return true;
}

SSLSocketStream::State SSLSocketStream::handle_ssl_error(std::unique_lock<std::mutex> &lk, int st) {
    int e  =SSL_get_error(_ssl.get(), st);
    switch (e) {
        case SSL_ERROR_NONE:return State::ready;
        case SSL_ERROR_ZERO_RETURN: return State::eof;
        case SSL_ERROR_WANT_READ: {
            lk.unlock();
            bool r = _socket.wait_read(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(_read_timeout).count()));
            lk.lock();
            return r?State::ready:State::timeout;
        }
        case SSL_ERROR_WANT_WRITE: {
            bool r = _socket.wait_write(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(_write_timeout).count()));
            return r?State::ready:State::timeout;
        }
        case SSL_ERROR_SSL: {
            unsigned long err = ERR_get_error();
            throw SSLException(ERR_error_string(err, NULL));
        }
        case SSL_ERROR_SYSCALL: {
            int err = errno;   
            if (err == EPIPE || err == ECONNRESET) {
                return State::eof ;
            }        
            throw std::system_error(err, std::system_category(), "SSL systcall error");
        }
        default:
            return State::eof;
    }
}

std::string_view SSLSocketStream::read() {
    if (!_unprocessed.empty() || _eof) {
        auto z = _unprocessed;
        _unprocessed = {};
        return z;
    }
    std::unique_lock lk(_sslmx);
    while (true) {
        int r = SSL_read(_ssl.get(), _input_buffer.data(), static_cast<int>(_input_buffer.size()));
        if (r <= 0) {
            auto st = handle_ssl_error(lk, r);
            if (st == State::timeout) return {};
            if (st == State::eof) {
                _eof = true;
                return {};
            }
        } else if (r > 0) {
            return {_input_buffer.data(),static_cast<std::size_t>(r)};            
        } 
    }
}

bool SSLSocketStream::write(std::string_view data) {
    std::scoped_lock _(_wrmx);
    if (_closed) return false;
    std::unique_lock lk(_sslmx);
    while (true) {
        int r = SSL_write(_ssl.get(), data.data(), static_cast<int>(data.size()));
        if (r <= 0) {
            auto st = handle_ssl_error(lk, r);
            if (st != State::ready) {
                _closed = true;
                return false;
            }
        } else if (r > 0) {
            data = data.substr(static_cast<std::size_t>(r));
            if (data.empty()) return true;
        } 
    }
}

void  SSLSocketStream::put_back(std::string_view data){
    _unprocessed = data;
}
void  SSLSocketStream::set_read_timeout(std::chrono::milliseconds timeout){
    _read_timeout = timeout;
}
void  SSLSocketStream::set_write_timeout(std::chrono::milliseconds timeout){
    _write_timeout = timeout;
}
void  SSLSocketStream::close(){
    std::unique_lock lk(_sslmx);
    int r= SSL_shutdown(_ssl.get());
    while (r < 1) {
        if (handle_ssl_error(lk, r) != State::ready) break;
        r= SSL_shutdown(_ssl.get());
    }
    ::shutdown(_socket, SHUT_RDWR);
    _eof = true;
    _closed = true;        
}


}