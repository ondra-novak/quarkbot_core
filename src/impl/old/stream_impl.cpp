#include "stream_impl.hpp"

namespace quarkbot {


struct Test: MarketStreamTypeItem {
    static constexpr Type type = "test";
    int x;
};

template class StreamClient<Test,1>;
template class StreamServer<Test,1>;

}