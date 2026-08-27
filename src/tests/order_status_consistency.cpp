/// The status a strategy acts on must equal the status a report shows.
/**
 * OrderRejectionReason travels through the update variant, and every consumer
 * used to derive the resulting OrderStatus on its own: OrderInternalData and
 * json_report hardcoded `rejected`, while the CSV report ran the reason through
 * update2status() and printed CANCEL for `expired` and `post_only_taker`. The
 * same event was therefore reported with two different statuses.
 *
 * This test places a post_only order that would take liquidity - the simulator
 * answers with OrderRejectionReason::post_only_taker - and checks that the
 * strategy and the CSV report agree.
 */
#include "check.h"
#include "quarkbot/backtest/backtest.hpp"
#include "quarkbot/backtest/simexec_report_csv.hpp"
#include "quarkbot/common/order_internal_defs.hpp"
#include "quarkbot/context.hpp"
#include "quarkbot/instrument_description.hpp"
#include "quarkbot/order_defs.hpp"
#include "quarkbot/strategy_fragment.hpp"
#include "quarkbot/stream/quote.hpp"
#include "quarkbot/tradable_instrument.hpp"
#include <array>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

using namespace quarkbot;

namespace {

struct Result {
    OrderStatus strategy_status = OrderStatus::unknown;
    OrderRejectionReason strategy_reason = OrderRejectionReason::none;
};

StrategyFragment strategy(StrategyContext ctx, Result *res) {
    TradableInstrument instr = ctx.instruments.at(0);
    auto stream = instr.subscribe<Quote>();
    Quote qt;
    do {
        if (!co_await stream.receive(qt)) co_return;
    } while (!qt.both_sides());

    //limit 110 against an ask of 101 would take liquidity -> post_only_taker
    Order o = instr.place_order(OrderRequest{
        .side = Side::buy, .type = OrderType::limit_post_only,
        .quantity = 4_dec, .limit_price = 110_dec});

    OrderReport rpt;
    while (co_await o.receive(rpt)) {}
    res->strategy_status = rpt.status;
    res->strategy_reason = rpt.rejection_reason;
}

}

int main() {
    auto t0 = std::chrono::system_clock::from_time_t(1780670224);

    std::vector<std::string> csv;
    std::array<WalletInitItem, 1> wallet{WalletInitItem{"USD", 1000000_dec}};
    std::array<InstrumentDescription, 1> instruments{InstrumentDescription{
        {}, {0.00001_dec, Decimal::max(), 0.00001_dec, 0.01_dec, 0, 0, 0, 0},
        {"USD"}, {"USD"}, {}, "TESTI", InstrumentCategory::Crypto}};

    Result res;
    SimulationParams sim;
    sim.reporter = open_report([&](std::string_view line){ csv.emplace_back(line); });

    {
        BacktestEnv bt("backtest", wallet, instruments, sim);
        bt.add_strategy([&](StrategyContext &&c){return strategy(std::move(c), &res);});
        int n = 0;
        bt.run([&](BacktestEvent &ev){
            if (n >= 2) return false;
            ev.symbol = "TESTI";
            ev.time = t0 + std::chrono::seconds(n);
            ev.data = Quote{100_dec, 10_dec, 101_dec, 10_dec, ev.time};
            ++n;
            return true;
        });
    }

    //the simulator must have answered with post_only_taker at all
    CHECK(res.strategy_reason == OrderRejectionReason::post_only_taker);

    //find the terminal row the CSV report wrote for that order
    std::string terminal;
    for (auto &line: csv) {
        if (line.find("post_only_taker") != std::string::npos) terminal = line;
    }
    CHECK(!terminal.empty());

    //both sides must name the same status
    OrderStatus expected = rejection_reason_2_status(OrderRejectionReason::post_only_taker);
    CHECK(res.strategy_status == expected);

    std::string csv_event = terminal.substr(terminal.find(',') + 1);
    csv_event = csv_event.substr(0, csv_event.find(','));
    std::cout << "strategy=" << string_lookup<OrderStatus>(res.strategy_status).value_or("?")
              << "  csv=" << csv_event << "\n";
    CHECK_EQUAL(csv_event, std::string(expected == OrderStatus::canceled?"CANCEL":"REJECT"));

    return 0;
}
