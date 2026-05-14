#pragma once

#include "impl/streaming/lock_free_publisher.hpp"
#include "ifc/market_instrument.hpp"
namespace quarkbot {
namespace bitfinex{

struct ClosedBarCalc {
    void operator()(LockFreePublisher<ClosedBar, 1> &q, const Trade &in, unsigned int param) const noexcept;
};

struct RangeBarCalc {
    void operator()(LockFreePublisher<RangeBarView, 1> &q, const Trade &in, Decimal param) const noexcept;
};

}
}