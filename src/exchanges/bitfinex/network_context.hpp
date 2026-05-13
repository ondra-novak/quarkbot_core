#pragma once

#include "libs/network/rest.hpp"
#include "libs/network/sslobjects.hpp"
#include "libs/network/ws.hpp"
#include "libs/network/ws_stream.hpp"
#include <initializer_list>
namespace quarkbot {
namespace bitfinex {
    
    class NetworkContext {
    public:
        NetworkContext(network::PSSL_CTX sslctx):_sslctx(std::move(sslctx)) {}

        static constexpr auto static_headers =   std::array<std::pair<std::string_view, std::string_view>,1> ({
                {"User-Agent", "quarkbot/1.0"}}
        );



        network::SecureRestClient create_rest() {
            network::SecureRestClient r(_sslctx, "https://api-pub.bitfinex.com/v2");
            r.add_headers(static_headers);
            return r;
        }

        network::PWebSocketSecure create_public_websocket() {
            return  network::wss_connect(_sslctx, "wss://api-pub.bitfinex.com/ws/2", static_headers, {});        
        }

    protected:
        network::PSSL_CTX _sslctx;
    };


}
}