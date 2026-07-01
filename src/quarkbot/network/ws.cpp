#include "ws.hpp"
#include "http_parser.hpp"
#include "http_status_exception.hpp"
#include "base64.hpp"
#include "crack_url.hpp"
#include "ws_stream.hpp"

#include <iterator>
#include <openssl/sha.h>
#include <random>
namespace network {

static constexpr std::string_view ws_guid("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

PWebSocketSecure wss_connect(const PSSL_CTX &sslctx,
        std::string_view wss_url, 
        std::span<const std::pair<std::string_view, std::string_view> > extra_headers,
        StreamConfiguration cfg) {

    auto cracked_url = crack_url(wss_url);
    if (!cracked_url.valid) throw WebSocketException("Secure websocket url invalid", std::string(wss_url));
    if (compare_icase(cracked_url.schema,"wss")) throw WebSocketException("Secure websocket url invalid (expecting wss://)", std::string(wss_url));

    std::string hoststr (cracked_url.host);
    std::string host_hdr = hoststr;
    if (!cracked_url.is_default_port) {
        hoststr.append(":");
        hoststr.append(std::to_string(cracked_url.port));
    }
    
    auto socket = connect(sslctx, hoststr, static_cast<std::uint16_t>(cracked_url.port));
    HttpBuilder bld;

    std::array<uint8_t, 16> rnd_key;
    {
        std::random_device rndseed;
        std::default_random_engine rnd(rndseed());
        for (auto &x: rnd_key) x = rnd() & 0xFF;
    }

    std::string wskey;    
    base64.encode(rnd_key.begin(), rnd_key.end(), std::back_inserter(wskey));
    std::string wskey_combined = wskey;
    wskey_combined.append(ws_guid);

    std::array<unsigned char, SHA_DIGEST_LENGTH> digest;
    SHA1(reinterpret_cast<const unsigned char *>(wskey_combined.data()), wskey_combined.size(), digest.data());

    std::string response_key;
    base64.encode(digest.begin(), digest.end(), std::back_inserter(response_key));


    


    bld.start_request("GET", cracked_url.path);
    bld.add_header("Host", host_hdr );
    bld.add_header("Connection","upgrade");
    bld.add_header("Upgrade","websocket");
    bld.add_header("Sec-WebSocket-Version",13);
    bld.add_header("Sec-WebSocket-Key", wskey);
    for (auto &[k,v]: extra_headers) {
        bld.add_header(k,v);
    }
    bld.finish();
//    std::cout << bld.get_result() << std::endl;
    if (!socket->write(bld.get_result())) throw WebSocketException("Connection RESET when sending websocket handshake", std::string(wss_url));    

    HttpParser parser;
    std::string_view buff;
    do {
        buff = socket.read();
        if (buff.empty()) throw WebSocketException("Connection RESET when receiving websocket handshake", std::string(wss_url));
  //      std::cout << buff << std::endl;
    } while (!parser(buff));

    if (parser.code() != 101) throw HttpStatusException(parser.code(), parser.message(), wss_url);
    auto hdr = parser.header("Connection");
    if (!hdr || compare_icase(hdr.value(),"upgrade")) WebSocketException("Invalid handshake (expecting Connection: upgrade)", std::string(wss_url));
    hdr = parser.header("Upgrade");
    if (!hdr || compare_icase(hdr.value(),"websocket")) WebSocketException("Invalid handshake (expecting Upgrade: websocket)", std::string(wss_url));
    hdr = parser.header("Sec-WebSocket-Accept");
    if (!hdr || hdr.value() != response_key) WebSocketException("Invalid handshake - Sec-WebSocket-Accept", std::string(wss_url));

    socket->put_back(parser.unprocessed());

    return std::make_shared<WebsocketStream< StreamWrapper<SSLSocketStream> > >(std::move(socket), StreamRole::client, cfg);



}


}