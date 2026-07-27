#pragma once

#include "ipublisher.hpp"
namespace quarkbot {

class IHistoryAdapter: public IPublisher {
public:

    class Null;

};

class IHistoryAdapter::Null final: public IHistoryAdapter {
public:
    virtual std::shared_ptr<IEventStreamBase> subscribe_stream(std::size_t , const void *) {return nullptr;}
};

}