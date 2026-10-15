#include "check.h"
#include "quarkbot/backtest/auction_phase_tracker.hpp"
#include <chrono>
#include <iostream>
#include <string>

using namespace quarkbot;
using Phase = AuctionPhaseTracker::Phase;
using Actions = AuctionPhaseTracker::Actions;
using namespace std::chrono_literals;

static const auto t0 = std::chrono::system_clock::from_time_t(1780670224);
static const std::string X = "X";
static const std::string Y = "Y";

static bool no_action(const Actions &a) {
    return !a.uncross && !a.finish && !a.close_day && !a.start_close_timeout;
}

static void test_unknown_instrument() {
    std::cout << "--- test_unknown_instrument" << std::endl;
    AuctionPhaseTracker tr;
    CHECK(tr.can_match_continuous(X));
    CHECK(!tr.is_closed(X));
    CHECK(tr.phase(X) == Phase::continuous);
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::not_tradable);
    CHECK(tr.check_auction_order(X, TimeInForce::atc) == OrderRejectionReason::not_tradable);
    CHECK(tr.check_auction_order(X, TimeInForce::crossing) == OrderRejectionReason::not_tradable);
    CHECK(no_action(tr.on_trade(X, TradeQualifier::continuous, t0)));
    //a continuous trade alone never makes the instrument an auction instrument
    CHECK(tr.check_auction_order(X, TimeInForce::atc) == OrderRejectionReason::not_tradable);
    CHECK(no_action(tr.on_close_timeout(X)));
}

static void test_indicative_unknown_ignored() {
    std::cout << "--- test_indicative_unknown_ignored" << std::endl;
    AuctionPhaseTracker tr;
    CHECK(no_action(tr.on_indicative(X, AuctionType::unknown)));
    CHECK(tr.can_match_continuous(X));
    CHECK(tr.check_auction_order(X, TimeInForce::atc) == OrderRejectionReason::not_tradable);
}

static void test_opening_with_indicative() {
    std::cout << "--- test_opening_with_indicative" << std::endl;
    AuctionPhaseTracker tr;
    CHECK(no_action(tr.on_indicative(X, AuctionType::opening)));
    CHECK(tr.phase(X) == Phase::call);
    CHECK(tr.call_type(X) == AuctionType::opening);
    CHECK(!tr.can_match_continuous(X));
    CHECK(!tr.check_auction_order(X, TimeInForce::ato).has_value());

    auto a = tr.on_trade(X, TradeQualifier::opening_auction, t0);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::opening);
    CHECK(!a.close_day);
    CHECK(!a.start_close_timeout);
    CHECK(tr.phase(X) == Phase::continuous);
    CHECK(tr.can_match_continuous(X));
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);
    CHECK(!tr.check_auction_order(X, TimeInForce::atc).has_value());
    CHECK(!tr.check_auction_order(X, TimeInForce::crossing).has_value());
}

static void test_opening_without_indicative() {
    std::cout << "--- test_opening_without_indicative" << std::endl;
    AuctionPhaseTracker tr;
    auto a = tr.on_trade(X, TradeQualifier::opening_auction, t0);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::opening);
    CHECK(tr.phase(X) == Phase::continuous);
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);
}

static void test_opening_did_not_happen() {
    std::cout << "--- test_opening_did_not_happen" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_indicative(X, AuctionType::opening);
    auto a = tr.on_trade(X, TradeQualifier::continuous, t0);
    CHECK(!a.uncross);
    CHECK(a.finish == AuctionType::opening);
    CHECK(!a.close_day);
    CHECK(tr.phase(X) == Phase::continuous);
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);
}

static void test_duplicate_opening() {
    std::cout << "--- test_duplicate_opening" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::opening_auction, t0);
    //a second print of the same opening (another market center) is a continuous trade
    CHECK(no_action(tr.on_trade(X, TradeQualifier::opening_auction, t0 + 1s)));
    CHECK(tr.phase(X) == Phase::continuous);
}

static void test_closing_with_indicative() {
    std::cout << "--- test_closing_with_indicative" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::opening_auction, t0);

    auto a = tr.on_indicative(X, AuctionType::closing);
    CHECK(a.start_close_timeout);
    CHECK(!a.uncross);
    CHECK(tr.phase(X) == Phase::call);
    CHECK(tr.call_type(X) == AuctionType::closing);
    CHECK(!tr.can_match_continuous(X));
    //the timeout is started once per day
    CHECK(no_action(tr.on_indicative(X, AuctionType::closing)));
    //a continuous trade in the closing call phase cannot happen on clean data - ignored
    CHECK(no_action(tr.on_trade(X, TradeQualifier::continuous, t0 + 1s)));
    CHECK(tr.phase(X) == Phase::call);

    a = tr.on_trade(X, TradeQualifier::closing_auction, t0 + 2s);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::closing);
    CHECK(a.close_day);
    CHECK(tr.is_closed(X));
    CHECK(tr.phase(X) == Phase::closed);
    CHECK(!tr.can_match_continuous(X));
    CHECK(tr.check_auction_order(X, TimeInForce::atc) == OrderRejectionReason::too_late);
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);

    //duplicate closing print: does nothing and must not reopen the day
    CHECK(no_action(tr.on_trade(X, TradeQualifier::closing_auction, t0 + 3s)));
    CHECK(tr.is_closed(X));
    //timeout after the uncross: nothing left to do
    CHECK(no_action(tr.on_close_timeout(X)));
    CHECK(tr.is_closed(X));
}

static void test_closing_without_indicative() {
    std::cout << "--- test_closing_without_indicative" << std::endl;
    AuctionPhaseTracker tr;
    auto a = tr.on_trade(X, TradeQualifier::closing_auction, t0);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::closing);
    CHECK(a.close_day);
    CHECK(!a.start_close_timeout);
    CHECK(tr.is_closed(X));
}

static void test_closing_timeout() {
    std::cout << "--- test_closing_timeout" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_indicative(X, AuctionType::closing);
    auto a = tr.on_close_timeout(X);
    CHECK(!a.uncross);
    CHECK(a.finish == AuctionType::closing);
    CHECK(a.close_day);
    CHECK(tr.is_closed(X));
    CHECK(no_action(tr.on_close_timeout(X)));
}

static void test_new_day_by_indicative_opening() {
    std::cout << "--- test_new_day_by_indicative_opening" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_indicative(X, AuctionType::closing);
    tr.on_trade(X, TradeQualifier::closing_auction, t0);
    CHECK(tr.is_closed(X));
    CHECK(no_action(tr.on_indicative(X, AuctionType::opening)));
    CHECK(!tr.is_closed(X));
    CHECK(tr.phase(X) == Phase::call);
    CHECK(tr.call_type(X) == AuctionType::opening);
    CHECK(!tr.check_auction_order(X, TimeInForce::ato).has_value());
    //the closing timeout is armed again on the new day
    CHECK(tr.on_indicative(X, AuctionType::closing).start_close_timeout);
}

static void test_new_day_by_opening_trade() {
    std::cout << "--- test_new_day_by_opening_trade" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::closing_auction, t0);
    auto a = tr.on_trade(X, TradeQualifier::opening_auction, t0 + 24h);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::opening);
    CHECK(!tr.is_closed(X));
    CHECK(tr.phase(X) == Phase::continuous);
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);
    CHECK(tr.on_indicative(X, AuctionType::closing).start_close_timeout);
}

static void test_new_day_by_continuous_trade() {
    std::cout << "--- test_new_day_by_continuous_trade" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::closing_auction, t0);
    CHECK(no_action(tr.on_trade(X, TradeQualifier::continuous, t0 + 24h)));
    CHECK(!tr.is_closed(X));
    CHECK(tr.can_match_continuous(X));
    //the opening auction was missed today
    CHECK(tr.check_auction_order(X, TimeInForce::ato) == OrderRejectionReason::too_late);
    CHECK(!tr.check_auction_order(X, TimeInForce::atc).has_value());
    //a late opening print is a duplicate now
    CHECK(no_action(tr.on_trade(X, TradeQualifier::opening_auction, t0 + 24h + 1s)));
}

static void test_indicative_ignored_while_closed() {
    std::cout << "--- test_indicative_ignored_while_closed" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::closing_auction, t0);
    CHECK(no_action(tr.on_indicative(X, AuctionType::closing)));
    CHECK(no_action(tr.on_indicative(X, AuctionType::intraday)));
    CHECK(tr.is_closed(X));
}

static void test_intraday_window() {
    std::cout << "--- test_intraday_window" << std::endl;
    AuctionPhaseTracker tr;
    auto a = tr.on_trade(X, TradeQualifier::intraday_auction, t0);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::intraday);
    CHECK(!a.close_day);
    CHECK(tr.phase(X) == Phase::continuous);
    //inside the window: a duplicate print from another market center
    CHECK(no_action(tr.on_trade(X, TradeQualifier::intraday_auction, t0 + 119s)));
    //the window is measured from the last uncross, and its end is exclusive
    a = tr.on_trade(X, TradeQualifier::intraday_auction, t0 + 2min);
    CHECK(a.uncross);
    //another auction type has its own window
    a = tr.on_trade(X, TradeQualifier::unscheduled_auction, t0 + 2min + 1s);
    CHECK(a.uncross);
    CHECK(a.finish == AuctionType::unscheduled);
    CHECK(no_action(tr.on_trade(X, TradeQualifier::unscheduled_auction, t0 + 2min + 30s)));
}

static void test_intraday_call_without_trade() {
    std::cout << "--- test_intraday_call_without_trade" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_indicative(X, AuctionType::intraday);
    CHECK(tr.phase(X) == Phase::call);
    auto a = tr.on_trade(X, TradeQualifier::continuous, t0);
    CHECK(!a.uncross);
    CHECK(a.finish == AuctionType::intraday);
    CHECK(tr.phase(X) == Phase::continuous);
    //an intraday auction says nothing about the opening auction
    CHECK(!tr.check_auction_order(X, TimeInForce::ato).has_value());
}

static void test_intraday_trade_keeps_closing_call() {
    std::cout << "--- test_intraday_trade_keeps_closing_call" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_indicative(X, AuctionType::closing);
    auto a = tr.on_trade(X, TradeQualifier::intraday_auction, t0);
    CHECK(a.uncross);
    CHECK(tr.phase(X) == Phase::call);
    CHECK(tr.call_type(X) == AuctionType::closing);
}

static void test_instruments_independent() {
    std::cout << "--- test_instruments_independent" << std::endl;
    AuctionPhaseTracker tr;
    tr.on_trade(X, TradeQualifier::closing_auction, t0);
    CHECK(tr.is_closed(X));
    CHECK(!tr.is_closed(Y));
    CHECK(tr.can_match_continuous(Y));
    CHECK(tr.check_auction_order(Y, TimeInForce::atc) == OrderRejectionReason::not_tradable);
}

int main() {
    test_unknown_instrument();
    test_indicative_unknown_ignored();
    test_opening_with_indicative();
    test_opening_without_indicative();
    test_opening_did_not_happen();
    test_duplicate_opening();
    test_closing_with_indicative();
    test_closing_without_indicative();
    test_closing_timeout();
    test_new_day_by_indicative_opening();
    test_new_day_by_opening_trade();
    test_new_day_by_continuous_trade();
    test_indicative_ignored_while_closed();
    test_intraday_window();
    test_intraday_call_without_trade();
    test_intraday_trade_keeps_closing_call();
    test_instruments_independent();
    return 0;
}
