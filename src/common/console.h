#pragma once

#include "read_line.h"

#include <quarkbot/mq.h>

#include <cstddef>
#include <mutex>
#include <string>
#include <set>
#include <stop_token>

namespace quarkbot {

class ConsoleClient:
        public ReadLine,
        public IMQBroker::IListener
            {
public:

    ConsoleClient(MQBroker broker, ReadLineConfig rlcfg);

    virtual void on_message(IMQBroker::Message message) override;

    void run(std::stop_token tkn);


protected:
    MQClient _client;
    void worker(std::stop_token tkn);

    struct HintCacheItem {
        std::size_t srl;
        std::string line;
        std::size_t pos;
        std::set<std::string> hints;
    };

    std::mutex _cache_lock;
    HintCacheItem _cache;
    std::size_t _cur_srl = 0;


    virtual bool onComplete(const char *wholeLine, std::size_t start, std::size_t end, const HintCallback &cb) noexcept override;



};


}
