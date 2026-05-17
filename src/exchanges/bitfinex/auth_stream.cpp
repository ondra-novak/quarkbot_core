#include "auth_stream.hpp"
#include "libs/network/ws_parser.hpp"
#include "network_context.hpp"
#include <stdexcept>
#include <stop_token>
#include <thread>

namespace quarkbot {
namespace bitfinex {


    void AuthStream::connect(NetworkContext ctx, std::string_view apikey, const Signer::ChannelSigned &signature, std::function<void(Json)> callback) {
        _callback = std::move(callback);
        _ws = ctx.create_auth_websocket();
        send_command(Json {
            {"apiKey", apikey},
            {"authSig", signature.signature},
            {"authNonce", signature.nonce},
            {"authPayload", signature.authPayload},
            {"event","auth"},
            {"filter",{
                "trading",
                "balance",
                "notify"}}
        });        

        while (true) {
            auto msg = _ws->receive();
            if (msg.type == network::FrameType::close) throw std::runtime_error("Bifinex: Failed to open authenticated channel - connection lost");
            if (msg.type == network::FrameType::text) {
                Json js = Json::from_string(msg.data);
                if (js["event"].as_text() =="auth") {
                    if (js["status"].as_text() == "FAILED") {
                        throw std::runtime_error(std::format("Bitfinex {} {}", js["code"].as_int(), js["msg"].as_text()));
                    }
                    break;
                }
            }
        }

        _worker = std::jthread([this](std::stop_token tkn){
            worker(tkn);
        });


    }
    bool AuthStream::send_command(const Json &jsonReq) {
        return _ws->send({network::FrameType::text, jsonReq.to_string()});
    }

    void AuthStream::worker(std::stop_token tkn) {
        std::stop_callback _(tkn,[this]{_ws->close();});
        try {
            while (!tkn.stop_requested()) {
                auto msg = _ws->receive();
                if (msg.type == network::FrameType::close) {
                    break;
                } else if (msg.type == network::FrameType::text) {
                    _callback(Json::from_string(msg.data));
                }
            }
            if (!tkn.stop_requested()) {
                _callback(nullptr);
            }            
        } catch (...) {
            if (!tkn.stop_requested()) {
                _callback(nullptr);     //under exception - can be detected
            }
        }
    }

    AuthStream::~AuthStream() {
        if (_worker.joinable()) {
            _worker.request_stop();
            if (_worker.get_id() == std::this_thread::get_id()) {
                _worker.detach();
            } else {
                _worker.join();
            }
        }
    }


}
}