#include "check.h"
#include "quarkbot/backtest/backtest_executor.hpp"
#include "quarkbot/backtest/siminstrument.hpp"
#include "quarkbot/backtest/simtradableinstrument.hpp"
#include "../quarkbot/backtest/simexec_report_csv.hpp"
#include "quarkbot/common/orderdata.hpp"
#include "quarkbot/order_defs.hpp"
#include <chrono>
#include <memory>

using namespace quarkbot;

constexpr std::string_view expected_output = 
"time,event,q,instrument,order,name,side,quantity,type,limit-price,stop-price,fill-price,fill-quantity,note\n"
"2026-06-05 14:37:04.000000000,NEW,✨,TestInstr,1234,ord1,BUY,123,LIMIT,456,,,\n"
"2026-06-05 14:37:04.000000000,FILL,,TestInstr,1234,ord1,BUY,123,LIMIT,456,,456,12\n"
"2026-06-05 14:37:04.000000000,FILLED,✅,TestInstr,1234,ord1,BUY,123,LIMIT,456,,,\n"
"2026-06-05 14:37:04.000000000,CANCEL,❌,TestInstr,1234,ord1,BUY,123,LIMIT,456,,,\n"
"2026-06-05 14:37:04.000000000,CANCEL,❌,TestInstr,1234,ord1,BUY,123,LIMIT,456,,,,expired\n"
"2026-06-05 14:37:04.000000000,REJECT,❌,TestInstr,1234,ord1,BUY,123,LIMIT,456,,,,other (Some error message)\n"
"2026-06-05 14:37:04.000000000,NEW,✨,TestInstr,2578,ord2,SELL,111,STOPLIMIT,687,689,,,Replace: 1234\n"
"2026-06-05 14:37:04.000000000,REPLACE,🔄,TestInstr,2578,ord2,SELL,111,STOPLIMIT,687,689,,\n"
"2026-06-05 14:37:04.000000000,REJECT,❌,TestInstr,2578,ord2,SELL,111,STOPLIMIT,687,689,,,insufficient_funds\n";

void test_report() {

    std::ostringstream out;
    std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(1780670224);

    SimInstrument::Info nfo;
    nfo.name = "TestInstr";

    std::shared_ptr<SimInstrument> instr = std::make_shared<SimInstrument>(nfo, nullptr);
    std::shared_ptr<SimTradableInstrument> tinstr = std::make_shared<SimTradableInstrument>(instr, std::shared_ptr<SimAccount>{});

    auto cancelcb = [](auto){};
    using CancelCB = decltype(cancelcb);
    using OrderData = OrderWithCancelCallback<CancelCB>;

    auto pord = std::make_shared<OrderData>(OrderParameters{"ord1",
        Side::buy,OrderType::limit,123,456
    }, std::static_pointer_cast<ITradableInstrument>(tinstr), POrder{}, tp, cancelcb);

    auto pord2 = std::make_shared<OrderData>(OrderParameters{"ord2",
        Side::sell,OrderType::stoplimit,111,687,689
    }, std::static_pointer_cast<ITradableInstrument>(tinstr), pord, tp,  cancelcb);
     
    auto ord = Order(pord);
    auto ord2 = Order(pord2);


    auto exec = BacktestExecutor::create();
    exec->set_time(tp);


    auto rep = open_report(out);
    auto opst = OrderOpenStatus{"1234",{}};
    auto opst2 = OrderOpenStatus{"2578",{}};

    rep(ord, opst);
    pord->forward_update(opst);
    rep(ord, Fill{{},"3432",pord->get_parameters().label,tp,nfo,Side::buy, {},12,456});
    rep(ord, OrderStatus::filled);
    rep(ord, OrderStatus::canceled);
    rep(ord, OrderRejectionReason::expired);
    rep(ord, OrderRejectionWithText{OrderRejectionReason::other,"Some error message"});
    rep(ord2, opst2);
    pord2->forward_update(opst2);
    rep(ord2, OrderStatus::replaced);
    rep(ord2, OrderRejectionReason::insufficient_funds);
    
    CHECK_EQUAL(expected_output, out.str());
}

int main() {
    test_report();
}