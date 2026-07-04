#include "../quarkbot/backtest/merged_data_source.hpp"
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "tests/check.h"
#include <chrono>
#include <iostream>
#include <vector>

using namespace quarkbot;
using tp = std::chrono::system_clock::time_point;

namespace {

class VectorSource {
public:
    explicit VectorSource(std::vector<BacktestEvent> events) : _events(std::move(events)) {}

    bool operator()(BacktestEvent &ev) {
        if (_idx >= _events.size()) return false;
        ev = _events[_idx++];
        return true;
    }

private:
    std::vector<BacktestEvent> _events;
    std::size_t _idx = 0;
};

}

static tp make_tp(long long ms) {
    return tp(std::chrono::milliseconds(ms));
}

static BacktestEvent make_trade_event(long long ms) {
    Trade t;
    t.price = Decimal(ms);
    t.size = Decimal(1);
    t.time = make_tp(ms);
    BacktestEvent ev;
    ev.time = make_tp(ms);
    ev.symbol = "SYM";
    ev.data = t;
    return ev;
}

int main() {
    std::vector<BacktestDataSource> sources;
    sources.emplace_back(VectorSource({make_trade_event(100), make_trade_event(300), make_trade_event(500)}));
    sources.emplace_back(VectorSource({make_trade_event(200), make_trade_event(400)}));
    // Source A: events at t=100, t=300, t=500
    // Source B: events at t=200, t=400
    MergedDataSource merged(std::move(sources));

    BacktestEvent ev;
    CHECK(merged(ev)); CHECK(ev.time == make_tp(100));
    CHECK(merged(ev)); CHECK(ev.time == make_tp(200));
    CHECK(merged(ev)); CHECK(ev.time == make_tp(300));
    CHECK(merged(ev)); CHECK(ev.time == make_tp(400));
    CHECK(merged(ev)); CHECK(ev.time == make_tp(500));
    CHECK(!merged(ev));

    // Empty source list
    MergedDataSource empty(std::vector<BacktestDataSource>{});
    BacktestEvent ev2;
    CHECK(!empty(ev2));

    std::cout << "All merged source tests passed" << std::endl;
}
