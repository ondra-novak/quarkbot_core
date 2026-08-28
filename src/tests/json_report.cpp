#include "check.h"
#include "quarkbot/backtest/backtest_executor.hpp"
#include "quarkbot/backtest/siminstrument.hpp"
#include "quarkbot/backtest/simtradableinstrument.hpp"
#include "../quarkbot/backtest/json_report.hpp"
#include "quarkbot/common/orderdata.hpp"
#include "quarkbot/order_defs.hpp"
#include <chrono>
#include <memory>
#include <sstream>

using namespace quarkbot;

///create_report_sink() is protected - the report is otherwise only reachable
///through a whole simulated exchange
class TestableJsonReport: public JsonReport {
public:
    using JsonReport::JsonReport;
    using JsonReport::create_report_sink;
};

static bool contains(const std::string &haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

///a stop order must report its own stop price, not the limit price
void test_stop_price_reported() {

    std::ostringstream out;
    std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(1780670224);

    SimInstrument::Info nfo;
    nfo.name = "TestInstr";

    auto instr = std::make_shared<SimInstrument>(nfo, nullptr);
    auto tinstr = std::make_shared<SimTradableInstrument>(instr, std::shared_ptr<SimAccount>{});

    auto cancelcb = [](auto){};
    using OrderData = OrderWithCancelCallback<decltype(cancelcb)>;

    //stoplimit: quantity 111, limit_price 687, stop_price 689
    auto pord = std::make_shared<OrderData>(OrderParameters{"ord1",
        Side::sell, OrderType::stoplimit, 111, 687, 689
    }, std::static_pointer_cast<ITradableInstrument>(tinstr), POrder{}, tp, cancelcb);

    auto ord = Order(pord);

    auto exec = BacktestExecutor::create();
    exec->set_time(tp);

    TestableJsonReport report(out);
    auto sink = report.create_report_sink();

    auto opst = OrderOpenStatus{"1234",{}};
    sink(ord, opst);

    const std::string text = out.str();
    CHECK(contains(text, "\"limit_price\":687"));
    CHECK(contains(text, "\"stop_price\":689"));
}

int main() {
    test_stop_price_reported();
}
