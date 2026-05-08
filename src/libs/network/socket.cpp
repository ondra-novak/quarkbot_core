#include "socket.hpp"
#include <cerrno>
#include <chrono>
#include <netdb.h>
#include <system_error>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

namespace network {


    bool Socket::wait_read( int timeoutms) {
        pollfd pfd = {_socket, POLLIN, 0};
        auto r = poll(&pfd,1,timeoutms);
        if (r < 0) throw std::system_error(errno, std::system_category(), "Poll read failed");
        return r > 0;
    }
    bool Socket::wait_write( int timeoutms) {
        pollfd pfd = {_socket, POLLOUT, 0};
        auto r = poll(&pfd,1,timeoutms);
        if (r < 0) throw std::system_error(errno, std::system_category(), "Poll write failed");
        return r > 0;
    }

    Socket Socket::connect(std::string host, std::uint16_t port, std::chrono::milliseconds connect_timeout) {
        struct addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        struct addrinfo *result;
        int ret = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
        if (ret != 0) {
            throw std::system_error(ret, std::system_category(), "getaddrinfo failed");
        }
        int fd = -1;
        for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
            fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd == -1) continue;
            // make non-blocking
            int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            // connect
            ret = ::connect(fd, rp->ai_addr, rp->ai_addrlen);
            if (ret == 0) {
                // connected immediately
                freeaddrinfo(result);
                return Socket(fd);
            } else if (errno == EINPROGRESS) {
                // wait with poll
                pollfd pfd = {fd, POLLOUT, 0};
                int timeout_ms = static_cast<int>(connect_timeout.count());
                ret = poll(&pfd, 1, timeout_ms);
                if (ret > 0) {
                    // check if connected
                    int err;
                    socklen_t len = sizeof(err);
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                    if (err == 0) {
                        freeaddrinfo(result);
                        return Socket(fd);
                    } else {
                        ::close(fd);
                        continue;
                    }
                } else if (ret == 0) {
                    ::close(fd);
                    continue;
                } else {
                    ::close(fd);
                    throw std::system_error(errno, std::system_category(), "Poll failed");
                }
            } else {
                ::close(fd);
                continue;
            }
        }
        freeaddrinfo(result);
        throw std::system_error(ECONNREFUSED, std::system_category(), "No address succeeded");        
    }

}