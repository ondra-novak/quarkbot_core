#pragma once

#include "mq/mq.h"
#include "read_line.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <set>
#include <stop_token>

namespace quarkbot {

class ConsoleClient:
        public ReadLine,
        public MQClient
            {
public:

    ConsoleClient(MQBroker broker, ReadLineConfig rlcfg);

    virtual void on_message(const IMQBroker::Message &message, bool pm) noexcept override;

    void run(std::stop_token tkn);


protected:
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
