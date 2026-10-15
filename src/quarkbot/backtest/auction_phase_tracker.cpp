#include "auction_phase_tracker.hpp"

namespace quarkbot {

void AuctionPhaseTracker::new_day(State &st) {
    st.phase = Phase::continuous;
    st.call_type = AuctionType::unknown;
    st.opening_done = false;
    st.close_timeout_started = false;
}

void AuctionPhaseTracker::end_call(State &st, AuctionType type) {
    if (st.phase != Phase::call || st.call_type == type) {
        st.phase = Phase::continuous;
        st.call_type = AuctionType::unknown;
    }
}

std::optional<AuctionPhaseTracker::Timestamp> &AuctionPhaseTracker::last_uncross(State &st, AuctionType type) {
    return type == AuctionType::intraday ? st.last_intraday_uncross : st.last_unscheduled_uncross;
}

AuctionPhaseTracker::Actions AuctionPhaseTracker::continuous_trade(State &st) {
    Actions a;
    switch (st.phase) {
        case Phase::closed:
            //clean data has no after-market, so a trade after the close is the
            //next day - which started without an opening auction
            new_day(st);
            st.opening_done = true;
            break;
        case Phase::call:
            //a continuous trade in the closing call cannot happen on clean data
            if (st.call_type == AuctionType::closing) break;
            //the auction ended without an uncross
            a.finish = st.call_type;
            if (st.call_type == AuctionType::opening) st.opening_done = true;
            st.phase = Phase::continuous;
            st.call_type = AuctionType::unknown;
            break;
        case Phase::continuous:
            break;
    }
    return a;
}

AuctionPhaseTracker::Actions AuctionPhaseTracker::on_indicative(const std::string &name, AuctionType type) {
    Actions a;
    if (type == AuctionType::unknown) return a;
    State &st = _states[name];
    if (st.phase == Phase::closed) {
        //after the close only the next day's opening auction is meaningful
        if (type != AuctionType::opening) return a;
        new_day(st);
    }
    if (type == AuctionType::opening && st.opening_done) return a;
    st.phase = Phase::call;
    st.call_type = type;
    if (type == AuctionType::closing && !st.close_timeout_started) {
        //without a closing trade the indicative stream simply stops and it
        //stays quiet until the next day - the timeout closes the day then
        st.close_timeout_started = true;
        a.start_close_timeout = true;
    }
    return a;
}

AuctionPhaseTracker::Actions AuctionPhaseTracker::on_trade(const std::string &name, TradeQualifier qualifier, Timestamp time) {
    if (!is_auction_trade(qualifier)) {
        auto iter = _states.find(name);
        if (iter == _states.end()) return {};
        return continuous_trade(iter->second);
    }

    AuctionType type = to_auction_type(qualifier);
    State &st = _states[name];
    //one auction may arrive as several prints (multiple market centers). The
    //first one runs the auction, every following one is a continuous trade.
    switch (type) {
        case AuctionType::opening:
            if (st.phase == Phase::closed) new_day(st);
            if (st.opening_done) return continuous_trade(st);
            break;
        case AuctionType::closing:
            //a duplicate after the close must not reopen the day
            if (st.phase == Phase::closed) return {};
            break;
        default: {
            const auto &last = last_uncross(st, type);
            if (last && time - *last < duplicate_window) return continuous_trade(st);
            break;
        }
    }

    Actions a;
    a.uncross = true;
    a.finish = type;
    switch (type) {
        case AuctionType::opening:
            st.opening_done = true;
            end_call(st, type);
            break;
        case AuctionType::closing:
            st.phase = Phase::closed;
            st.call_type = AuctionType::unknown;
            a.close_day = true;
            break;
        default:
            last_uncross(st, type) = time;
            end_call(st, type);
            break;
    }
    return a;
}

AuctionPhaseTracker::Actions AuctionPhaseTracker::on_close_timeout(const std::string &name) {
    auto iter = _states.find(name);
    if (iter == _states.end()) return {};
    State &st = iter->second;
    if (st.phase == Phase::closed) return {};
    st.phase = Phase::closed;
    st.call_type = AuctionType::unknown;
    Actions a;
    a.finish = AuctionType::closing;
    a.close_day = true;
    return a;
}

bool AuctionPhaseTracker::can_match_continuous(const std::string &name) const {
    auto iter = _states.find(name);
    return iter == _states.end() || iter->second.phase == Phase::continuous;
}

bool AuctionPhaseTracker::is_closed(const std::string &name) const {
    auto iter = _states.find(name);
    return iter != _states.end() && iter->second.phase == Phase::closed;
}

std::optional<OrderRejectionReason> AuctionPhaseTracker::check_auction_order(const std::string &name, TimeInForce tif) const {
    auto iter = _states.find(name);
    //no auction seen yet - the instrument is not known to have auctions
    if (iter == _states.end()) return OrderRejectionReason::not_tradable;
    const State &st = iter->second;
    if (st.phase == Phase::closed) return OrderRejectionReason::too_late;
    if (tif == TimeInForce::ato && st.opening_done) return OrderRejectionReason::too_late;
    return std::nullopt;
}

AuctionPhaseTracker::Phase AuctionPhaseTracker::phase(const std::string &name) const {
    auto iter = _states.find(name);
    return iter == _states.end() ? Phase::continuous : iter->second.phase;
}

AuctionType AuctionPhaseTracker::call_type(const std::string &name) const {
    auto iter = _states.find(name);
    return iter == _states.end() ? AuctionType::unknown : iter->second.call_type;
}

}
