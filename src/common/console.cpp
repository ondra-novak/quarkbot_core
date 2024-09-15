
#include "console.h"

#include <thread>
#include <quarkbot/interactive.h>
namespace quarkbot {



void ConsoleClient::on_message(const quarkbot::IMQBroker::Message &message, bool ) noexcept {
    if (message.get_channel() == "stdout") {
        std::cout << "[" << message.get_sender() << "] " << message.get_content() << std::endl;
    } else if (message.get_channel().empty()) {
        auto [srl, hint] = Interactive::HintResponseTuple::parse(message.get_content());
        std::lock_guard _(_cache_lock);
        if (_cache.srl == srl) {
            _cache.hints.insert(std::string(hint));
        }
    }

}

ConsoleClient::ConsoleClient(MQBroker broker, ReadLineConfig rlcfg)
:ReadLine(std::move(rlcfg))
,MQClient(broker)
{
    subscribe("stdout");
}

void ConsoleClient::run(std::stop_token tkn) {
    worker(std::move(tkn));
}


void ConsoleClient::worker(std::stop_token tkn) {
    std::stop_callback _stpcb(tkn, [&]{
        this->interrupt();
    });
    std::string line;
    while (this->read(line)) {
        this->send_message( "stdin", line);
    }
}

bool ConsoleClient::onComplete(const char *wholeLine, std::size_t start,
        std::size_t end, const HintCallback &cb) noexcept {
    std::unique_lock lk(_cache_lock);
    std::string_view line{wholeLine, std::max(start,end)};
    auto pos = std::min(start,end);
    if (_cache.line != line && _cache.pos != pos) {
        _cache.hints.clear();
        _cache.line = line;
        _cache.pos = pos;
        _cache.srl = ++_cur_srl;
        this->send_message( "stdin_hint",
                Interactive::HintRequestTuple::compose(_cache.srl, _cache.pos, _cache.line));
    }
    lk.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    lk.lock();
    for (const auto &x: _cache.hints) {
        cb(x);
    }
    return true;
}

}
