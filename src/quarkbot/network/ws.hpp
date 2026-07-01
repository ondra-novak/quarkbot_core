#pragma once

#include "ssl_stream.hpp"
#include "sslobjects.hpp"
#include "ws_parser.hpp"
#include "ws_stream.hpp"
#include <stdexcept>
#include <thread>
namespace network {



using PWebSocketSecure = std::shared_ptr<WebsocketStream<StreamWrapper<SSLSocketStream> > >;

PWebSocketSecure wss_connect(const PSSL_CTX &sslctx, 
        std::string_view wss_url, 
        std::span<const std::pair<std::string_view, std::string_view> > extra_headers, 
        StreamConfiguration cfg);
        
template<std::invocable<Message> Handler>
auto set_handler(PWebSocketSecure ws, Handler handler) {
    return std::thread([ws, handler = std::move(handler)]() mutable {
        Message msg;
        do {
            msg = ws->receive();
            handler(msg);
        } while (msg.type != FrameType::close);
        
    });
}

class WebSocketException: public std::runtime_error {
public:
    WebSocketException(std::string message, std::string url)
        :std::runtime_error("Websocket :" + message + " ("+url+")")
        ,message(std::move(message))
        ,url(std::move(url)) {}
    
    const std::string &get_message() const {return message;}
    const std::string &get_url() const {return url;}

protected:
    std::string message;
    std::string url;
};

}