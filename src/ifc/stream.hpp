#pragma once

#include "defs.hpp"
#include <chrono>

namespace quarkbot {

///Market event stream
/**
@tparam T type of event data
 */
template<StreamType T>
class IMarketEventStream {
public:
    virtual ~IMarketEventStream();

    ///Event structure
    struct Event {
        ///time of receiving
        std::chrono::system_clock::time_point received;
        ///number of missed events if processing was too slow
        std::size_t missed;
        ///event data
        T data;
    };

    ///Read next event
    /**
    @return awaitable returning next event (asynchronous). Returns std::nullopt on eof (awaitable acts as optional).
    If there are missed events, it immediately returns last received event with missed count set appropriately
    There can be only one pending read at a time. If you need multiple concurrent reads, create multiple streams.
     */
    virtual coro::awaitable<Event> read() = 0;
    ///Close the stream
    /**
       If there is pending read, it is immediately finished with nullopt
    */
    virtual void close() = 0;

};

}