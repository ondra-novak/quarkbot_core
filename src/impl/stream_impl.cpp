#include "stream_impl.hpp"
#include "coro/src/basic_coro/prepared_coro.hpp"
#include "ifc/defs.hpp"
#include <chrono>
#include <mutex>
#include <optional>

namespace quarkbot {


struct Test: MarketStreamTypeItem {
    static constexpr Type type = "test";
    int x;
};

template class StreamClient<Test,1>;
template class StreamServer<Test,1>;

}