#include "impl/merged_data_source.hpp"
#include "ifc/backtest_data_source.hpp"
#include "tests/check.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <vector>

using namespace quarkbot;
using tp = std::chrono::system_clock::time_point;

struct StubSource : public IBacktestDataSource {
    std::vector<Event> events;
    std::size_t idx = 0;

    explicit StubSource(std::vector<Event> evts) : events(std::move(evts)) {}

    std::optional<Event> next_event() override {
        if (idx >= events.size()) return std::nullopt;
        return events[idx++];
    }

    std::vector<InstrumentSpec> get_instrument_infos() override {
        InstrumentSpec s;
        s.name = "SYM";
        s.quote_currency = "USD";
        s.pnl_currency = "USD";
        return {s};
    }
};

static tp make_tp(long long ms) {
    return tp(std::chrono::milliseconds(ms));
}

static IBacktestDataSource::Event make_trade_event(long long ms) {
    Trade t;
    t.price = Decimal(ms);
    t.size = Decimal(1);
    t.time = make_tp(ms);
    IBacktestDataSource::Event ev;
    ev.time = make_tp(ms);
    ev.instrument = "SYM";
    ev.payload = t;
    return ev;
}

int main() {
    // Source A: events at t=100, t=300, t=500
    auto a = std::make_shared<StubSource>(std::vector<IBacktestDataSource::Event>{
        make_trade_event(100),
        make_trade_event(300),
        make_trade_event(500),
    });

    // Source B: events at t=200, t=400
    auto b = std::make_shared<StubSource>(std::vector<IBacktestDataSource::Event>{
        make_trade_event(200),
        make_trade_event(400),
    });

    MergedDataSource merged({a, b});

    auto e1 = merged.next_event(); CHECK(e1.has_value()); CHECK(e1->time == make_tp(100));
    auto e2 = merged.next_event(); CHECK(e2.has_value()); CHECK(e2->time == make_tp(200));
    auto e3 = merged.next_event(); CHECK(e3.has_value()); CHECK(e3->time == make_tp(300));
    auto e4 = merged.next_event(); CHECK(e4.has_value()); CHECK(e4->time == make_tp(400));
    auto e5 = merged.next_event(); CHECK(e5.has_value()); CHECK(e5->time == make_tp(500));
    auto e6 = merged.next_event(); CHECK(!e6.has_value());

    // get_instrument_infos deduplication: both sources return "SYM" — should give 1 result
    auto a2 = std::make_shared<StubSource>(std::vector<IBacktestDataSource::Event>{});
    auto b2 = std::make_shared<StubSource>(std::vector<IBacktestDataSource::Event>{});
    MergedDataSource merged2(std::vector<std::shared_ptr<IBacktestDataSource>>{a2, b2});
    auto specs = merged2.get_instrument_infos();
    CHECK_EQUAL(specs.size(), std::size_t(1));
    CHECK_EQUAL(specs[0].name, std::string("SYM"));

    // Empty source
    MergedDataSource empty({});
    CHECK(!empty.next_event().has_value());

    std::cout << "All merged source tests passed" << std::endl;
}
