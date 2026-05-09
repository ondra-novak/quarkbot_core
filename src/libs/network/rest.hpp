#pragma once

#include "libs/network/sslobjects.hpp"
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

        SecureRestClient(const PSSL_CTX &ctx, std::string hostport)
            :RestClient<RestClientSecureStreamFactory>(hostport, prepare_factory(ctx,hostport)) {}

    protected:
        static RestClientSecureStreamFactory prepare_factory(const PSSL_CTX &ctx, std::string_view hostport);
        
    };


}