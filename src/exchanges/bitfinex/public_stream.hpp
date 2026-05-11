#pragma once

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

        PublicStream(network::PSSL_CTX ctx);
        ~PublicStream();

        State subscribe_ticker(std::string symbol, Callback callback);
        State subscribe_trades(std::string symbol, Callback callback);        

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

        using Callback_Map =  std::unordered_map<int, Callback>;
        network::PSSL_CTX _sslctx;        
        std::thread _thr;
        std::stop_source _stpsrc;
        network::PWebSocketSecure _ws;
        std::mutex _mx;
        std::mutex _submx;
        Callback_Map _callbacks;        
        bool _closed = false;
        bool _closing = false;
        std::atomic<std::promise<int> *>subscribe_promise = {};
        void worker(std::stop_token tkn);        

        State open();
        int send_request(Json req, std::unique_lock<std::mutex> &lk);
        void cleanup();





    };

}
}