#pragma once

#include "network_context.hpp"
#include "signer.hpp"
#include "libs/network/ws.hpp"
#include "utils/json.hpp"
#include <functional>
namespace quarkbot {
namespace bitfinex {


class AuthStream  {
public:


    void connect(NetworkContext ctx, std::string_view apikey, const Signer::ChannelSigned &signature, std::function<void(Json)> callback);
    bool send_command(const Json &jsonReq);

    ~AuthStream();

protected:
    network::PWebSocketSecure _ws;
    std::function<void(Json)> _callback;
    std::jthread _worker;

    void worker(std::stop_token tkn);


};

}
}