#include "rest.hpp"
#include "crack_url.hpp"
#include "ssl_stream.hpp"

namespace network {

RestClientSecureStreamFactory SecureRestClient::prepare_factory(const PSSL_CTX &ctx, std::string_view host, unsigned int port) {
    return RestClientSecureStreamFactory {ctx,std::string(host), port};
}

StreamWrapper<SSLSocketStream> RestClientSecureStreamFactory::operator()() const {
    return connect(ctx,std::string(host), static_cast<std::uint16_t>(port));
}    

SecureRestClient::HostPortPrefix SecureRestClient::parse_url(std::string url) {
    auto parts = crack_url(url);
    if (!parts.valid) throw std::runtime_error("Invalid URL: " + url);
    if (parts.schema != "https") throw std::runtime_error("Invalid URL schema (expecting https): " + url);
    std::string host_hdr (parts.host);
    if (!parts.is_default_port) {
        host_hdr.append(":");
        host_hdr.append(std::to_string(parts.port));
    }   

    return HostPortPrefix{host_hdr, std::string(parts.host), parts.port, std::string(parts.path)};
}

}

