#pragma once

#include "exchanges/bitfinex/network_context.hpp"
#include "libs/network/sslobjects.hpp"
#include "libs/network/ws.hpp"
#include "utils/json.hpp"
#include <functional>
#include <future>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
namespace quarkbot {
namespace bitfinex {

    class PublicStream {
    public:

        using Callback = std::function<bool(Json)>;

        enum State {
            ok, full, closed, closing
        };

        PublicStream(NetworkContext ctx);
        ~PublicStream();

        State subscribe_ticker(std::string symbol, Callback callback);
        State subscribe_trades(std::string symbol, Callback callback);        
        State subscribe_orderbook(std::string symbol, Callback callback);



        class Exception: public std::runtime_error {
        public:
            Exception(int code, std::string message)
                :std::runtime_error("Bifinex stream error: "+ std::to_string(code) + " " + message)
                ,code(code)
                ,message(std::move(message)) {}

            int get_code() const {return code;}
            const std::string &get_message() const {return message;}

        protected: 
            int code;
            std::string message;
        };

    protected:

        struct AwaitingReg {
            Callback cb;
            std::promise<void> prom = {};
        };

        using Callback_Map =  std::unordered_map<int, Callback>;
        NetworkContext _sslctx;        
        std::thread _thr;
        std::stop_source _stpsrc;
        network::PWebSocketSecure _ws;
        std::mutex _mx;
        std::mutex _submx;
        Callback_Map _callbacks;        
        bool _closed = false;
        std::atomic<AwaitingReg *> pending_subscribe = {};
        void worker(std::stop_token tkn);        

        State open();
        void send_request(Json req, std::unique_lock<std::mutex> &lk, Callback cb);
        void cleanup();





    };

}
}