#pragma once

#include "sslobjects.hpp"
#include "rest_client.hpp"
#include "ssl_stream.hpp"
#include "stream_concept.hpp"
namespace network {


    struct RestClientSecureStreamFactory {
        PSSL_CTX ctx;
        std::string host;
        unsigned int port;
        StreamWrapper<SSLSocketStream> operator()() const;
    };

    ///Rest client through https
    /**
        Not MT Safe object. 
        You need to finish request before sending another
        Only pending request at time
    */
    class SecureRestClient: public RestClient<RestClientSecureStreamFactory> {
    public:

        struct HostPortPrefix {
            std::string host_hdr;
            std::string host;
            unsigned int port;
            std::string path_prefix;
        };

        SecureRestClient(const PSSL_CTX &ctx, std::string url):SecureRestClient(ctx, parse_url(std::move(url))) {}
        SecureRestClient(const PSSL_CTX &ctx, HostPortPrefix hpp)
            :RestClient<RestClientSecureStreamFactory>(std::move(hpp.host_hdr), std::move(hpp.path_prefix), prepare_factory(ctx,hpp.host,hpp.port)) {}

    protected:
        static RestClientSecureStreamFactory prepare_factory(const PSSL_CTX &ctx, std::string_view host, unsigned int port  );
        static HostPortPrefix parse_url(std::string url);
        
    };


}