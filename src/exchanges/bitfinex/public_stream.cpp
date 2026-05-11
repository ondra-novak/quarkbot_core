#include "public_stream.hpp" 
#include "libs/network/ws.hpp"
#include "libs/network/ws_parser.hpp"
#include <exception>
#include <future>
#include <mutex>
#include <stop_token>
#include <thread>

namespace quarkbot {

namespace bitfinex {

PublicStream::PublicStream(network::PSSL_CTX ctx):_sslctx(std::move(ctx)) {

}



PublicStream::State PublicStream::open() {
    if (_closed) return closed;
    if ( _callbacks.size() > 25) return full;
    if (_ws == nullptr) {
        _ws = network::wss_connect(_sslctx, "wss://api-pub.bitfinex.com/ws/2", {}, {});        
        _thr = std::thread([&]{
            worker(_stpsrc.get_token());
        });
    }
    return ok;

}

PublicStream::State PublicStream::subscribe_ticker(std::string symbol, Callback callback) {
    std::scoped_lock _(_submx);
    std::unique_lock  lk(_mx);
    State st = open();
    if (st!=ok) return st;
    Json req = {
        {"event","subscribe"},
        {"channel","ticker"},
        {"symbol", symbol}
    };
    int channel = send_request(std::move(req), lk);
    _callbacks.emplace(channel, std::move(callback));
    return st;



}
PublicStream::State PublicStream::subscribe_trades(std::string symbol, Callback callback){
    std::scoped_lock _(_submx);
    std::unique_lock  lk(_mx);
    State st = open();
    if (st!=ok) return st;
    Json req = {
        {"event","subscribe"},
        {"channel","trades"},
        {"symbol", symbol}
    };
    int channel = send_request(std::move(req), lk);
    _callbacks.emplace(channel, std::move(callback));
    return st;

}

int PublicStream::send_request(Json req, std::unique_lock<std::mutex> &lk) {
    std::promise<int> result;
    subscribe_promise = &result;
    _ws->send({network::FrameType::text,req.to_string()});
    auto f = result.get_future();
    lk.unlock();
    int res = f.get(); //TODO consider timeout
    lk.lock();
    return res;
}

void PublicStream::worker(std::stop_token tkn) {
    std::stop_callback _(tkn,[&]{
        _ws->close();
    });
    while (!tkn.stop_requested()) {
        auto msg = _ws->receive();
        if (msg.type == network::FrameType::text) {

            try {
                Json jmsg = Json::from_string(msg.data);
                if (jmsg.is_object()) {
                    auto event = jmsg["event"];
                    if (event.is_string()) {
                        if (event.as_text() == "subscribed") {
                            auto p = subscribe_promise.exchange(nullptr);
                            if (p) {
                                p->set_value(jmsg["chanId"].as_int());
                            }
                        }
                        else if (event.as_text() == "error") {
                            auto p = subscribe_promise.exchange(nullptr);
                            if (p) {
                                p->set_exception(std::make_exception_ptr(Exception(jmsg["code"].as_int(), jmsg["msg"].as<std::string>())));
                            }
                        }
                    }
                } else if (jmsg.is_array()) {
                    int channel = jmsg[0].as_int();
                    Json data = jmsg[1];
                    Callback *cb = nullptr;
                    {
                        std::scoped_lock _(_mx);
                        auto iter = _callbacks.find(channel);
                        if (iter != _callbacks.end()) cb = &iter->second;
                    }
                    bool keep = false;;
                    if (cb) {
                        try {
                            keep = (*cb)(data);
                            if (tkn.stop_requested()) return;
                        } catch (...) {
                            keep = false;
                        }
                    }
                    if (!keep) {
                        Json req = {
                            {"event","unsubscribe"},
                            {"chanId", channel}
                        };
                        _ws->send({network::FrameType::text, req.to_string()});
                        std::scoped_lock _(_mx);
                        _callbacks.erase(channel);
                        if (_callbacks.empty()) {
                            //slot is empty, close it
                            _ws->close();                            
                            _closed = true;
                            return;
                        }
                    }
                }
            } catch (...) {
                break;
            }


        } else if (msg.type == network::FrameType::close) {
            break;
        }
    }

    if (!tkn.stop_requested()) { //exit without stop token - stream closed        
        {
            std::scoped_lock _(_mx);
            _closing = true; //this thread is busy now, used for reconnect
        }
        //signal reconnect
        for (auto &[k, v]: _callbacks) {
            v(Json());
        }
    }
    return;
}

void PublicStream::cleanup() {
    Callback_Map tmp;
    {
        std::scoped_lock _(_mx);
        if (_ws) _ws->close();
        std::swap(tmp, _callbacks);
        _closed = true;
    }
}

PublicStream::~PublicStream() {
    _stpsrc.request_stop();    
    cleanup();
    if (std::this_thread::get_id() == _thr.get_id()) {
        _thr.detach();
    } else {
        _thr.join();
    }


}

}
}