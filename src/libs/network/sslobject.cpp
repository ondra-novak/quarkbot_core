#include "sslobjects.hpp"
#include <openssl/err.h>
#include <stdexcept>
#include "ws_stream.hpp"

namespace network {

    std::string SSLException::get_ssl_error() {
        auto err = ERR_get_error();
        const char *c = ERR_error_string(err, NULL);
        return c;
    }


    PSSL_CTX ssl_init_server(const std::string &server_ctr, const std::string &server_key) {
        PSSL_CTX ctx (SSL_CTX_new(TLS_server_method()));
        if (SSL_CTX_use_certificate_file(ctx.get(), server_ctr.c_str(), SSL_FILETYPE_PEM) <= 0) {
            throw SSLException();
        }
        if (SSL_CTX_use_PrivateKey_file(ctx.get(), server_key.c_str(), SSL_FILETYPE_PEM) <= 0) {
            throw SSLException();
        }
        if (!SSL_CTX_check_private_key(ctx.get())) {
            throw SSLException("Certificate doesn't match to private key");
        }
        return ctx;
}

    PSSL_CTX ssl_init_client() {
        PSSL_CTX ctx(SSL_CTX_new(TLS_client_method()));
        if (SSL_CTX_set_default_verify_paths(ctx.get()) != 1) {
                    throw SSLException();
        }
        return ctx;
    }

}