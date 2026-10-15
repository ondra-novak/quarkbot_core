#pragma once

#include <quarkbot/order_defs.hpp>
#include <quarkbot/stream/auction.hpp>
#include <quarkbot/stream/trade.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

namespace quarkbot {

///Tracks the auction phase of every instrument for the simulator
/**
 * A pure state machine with no knowledge of orders. It takes market events
 * (indicative auctions, trades, the closing timeout) and returns the Actions
 * SimExecutor has to carry out on its orders.
 *
 * Keyed by instrument name, not by SimInstrument pointer: instances are cached
 * through a weak_ptr and can be recreated, while the name is the stable identity.
 *
 * An instrument is stored only once it had an auction event (indicative or
 * trade). Until then it trades continuously and auction orders are rejected.
 *
 * Input data is assumed clean: no pre-market, no after-market and no trades
 * during an auction call phase.
 */
class AuctionPhaseTracker {
public:
    using Timestamp = std::chrono::system_clock::time_point;

    ///An intraday or unscheduled print of the same type within this window
    ///after the last uncross is a duplicate (another market center).
    /** Real intraday auctions follow halts with pauses of about 5 minutes. */
    static constexpr std::chrono::system_clock::duration duplicate_window = std::chrono::minutes(2);

    enum class Phase {
        ///continuous trading - regular orders are matched
        continuous,
        ///call phase of an auction - nothing is matched, orders are collected
        call,
        ///the trading day is closed - nothing is matched
        closed
    };

    ///What SimExecutor has to do after an event, in this order
    struct Actions {
        ///run the uncross at the price and size of the trade just reported
        bool uncross = false;
        ///finish this auction - expire the unfilled auction orders valid for it
        std::optional<AuctionType> finish;
        ///close the trading day - expire every order except gtc
        bool close_day = false;
        ///start the closing timeout from the event's time
        bool start_close_timeout = false;
    };

    ///Indicative auction data arrived
    Actions on_indicative(const std::string &name, AuctionType type);
    ///A trade arrived (continuous or auction)
    Actions on_trade(const std::string &name, TradeQualifier qualifier, Timestamp time);
    ///The closing timeout started by on_indicative() expired
    Actions on_close_timeout(const std::string &name);

    ///true when regular orders may be matched against quotes and trades
    bool can_match_continuous(const std::string &name) const;
    ///true after the close of the trading day
    bool is_closed(const std::string &name) const;
    ///Decide whether an order with an auction time in force may rest
    /** @return rejection reason, or nullopt when the order may rest */
    std::optional<OrderRejectionReason> check_auction_order(const std::string &name, TimeInForce tif) const;

    ///current phase, continuous for an unknown instrument
    Phase phase(const std::string &name) const;
    ///auction type of the call phase, AuctionType::unknown outside of it
    AuctionType call_type(const std::string &name) const;

protected:

    struct State {
        Phase phase = Phase::continuous;
        AuctionType call_type = AuctionType::unknown;
        ///today's opening auction is over - whether it happened or not
        bool opening_done = false;
        ///today's closing timeout is already running
        bool close_timeout_started = false;
        std::optional<Timestamp> last_intraday_uncross;
        std::optional<Timestamp> last_unscheduled_uncross;
    };

    std::unordered_map<std::string, State> _states;

    static void new_day(State &st);
    ///leave the call phase of the given auction (a call phase of another auction is kept)
    static void end_call(State &st, AuctionType type);
    static Actions continuous_trade(State &st);
    static std::optional<Timestamp> &last_uncross(State &st, AuctionType type);
};

}
