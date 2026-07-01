
#include "check.h"
#include "quarkbot/json/json.hpp"
#include "quarkbot/network/sslobjects.hpp"
#include "quarkbot/network/ws.hpp"

#include <iostream>

static network::PSSL_CTX ctx;

void connect_and_read_data() {
    auto stream = network::wss_connect(ctx, "wss://fstream.binance.com/market/ws", {}, {65536});
    Json subreq = {
        {"method","SUBSCRIBE"},
        {"params", { "btcusdt@aggTrade"}},
        {"id", 1}
    };
    stream->send({network::FrameType::text, subreq.to_string()});
    
    bool response_received = false;
    bool data_received = false;

    do {
        auto msg = stream->receive();
        CHECK(msg.fin);
        CHECK(msg.type == network::FrameType::text);
        std::cout << msg.data << std::endl;
        Json resp = Json::from_string(msg.data);
        if (resp["id"].is_number()) {
            response_received = true;
            CHECK_EQUAL(resp["id"].as_int() ,1);
        } else if (resp["e"].is_string()) {
            data_received = true;
            CHECK_EQUAL(resp["e"].as_text(), "aggTrade");
            
        }
    } while (!response_received || !data_received );

    std::atomic<bool> done = false;
    auto thr = network::set_handler(stream, [&](const network::Message &msg){
        if (msg.type == network::FrameType::text) {
            Json resp = Json::from_string(msg.data);
            CHECK_EQUAL(resp["e"].as_text(), "aggTrade");
            done = true;
            done.notify_all();
            
        }           
    });
    done.wait(false);
    stream->close();
    thr.join();
}



int main() {
    ctx = network::ssl_init_client();
    connect_and_read_data();
}