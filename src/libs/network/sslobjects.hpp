#pragma once

#include <memory>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/types.h>
#include <stdexcept>

namespace network {

    struct SSLSocketDeleter {
        void operator()(SSL *ptr) const {
            SSL_shutdown(ptr);            
            SSL_free(ptr);
        }
    };
    struct SSLContextDeleter {
        void operator()(SSL_CTX *ptr) const {
            SSL_CTX_free(ptr);
        }
    };

    using PSSL = std::unique_ptr<SSL, SSLSocketDeleter>;
    using PSSL_CTX = std::unique_ptr<SSL_CTX, SSLContextDeleter>;


    class SSLException : std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };
}
