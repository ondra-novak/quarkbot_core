#include "rest.hpp"
#include "libs/network/ssl_stream.hpp"

namespace network {

RestClientSecureStreamFactory SecureRestClient::prepare_factory(const PSSL_CTX &ctx, std::string_view hostport) {
    auto sep = hostport.find(":");
    std::string_view host;
    unsigned int port;

    if (sep == hostport.npos) {
        host = hostport;
        port = 443;
    } else {
        host = hostport.substr(0,sep);
        auto r = std::from_chars(hostport.begin()+sep+1, hostport.end(), port, 10);
        if (r.ec != std::errc{}) throw std::runtime_error("RestClient invalid port in hostport");        
    } 

    return RestClientSecureStreamFactory {ctx,std::string(host), port};
}

StreamWrapper<SSLSocketStream> RestClientSecureStreamFactory::operator()() const {
    return connect(ctx,std::string(host), static_cast<std::uint16_t>(port));
}    



}

