#include "trth_event_source.hpp" 
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/utils/string_utils.hpp"
#include "trth_raw_source.hpp"
#include <chrono>
#include <optional>
#include <sstream>
#include <utility>

namespace quarkbot {

TRTHEventSource::TRTHEventSource(std::filesystem::path file)
:_raw_source(std::move(file))
{
    
}

template<std::invocable<std::string_view, std::string_view> Callback>
void parse_qualifiers(std::string_view subj, Callback cb)
{
    std::size_t token_begin = 0;
    bool in_quotes = false;

    auto parse_token = [&](std::string_view token)
    {
        auto open = std::string_view::npos;

        bool quoted = false;
        for (std::size_t i = 0; i < token.size(); ++i) {
            if (token[i] == '"') {
                quoted = !quoted;
            } else if (token[i] == '[' && !quoted) {
                open = i;
                break;
            }
        }

        if (open == std::string_view::npos)
            return;

        auto close = token.find(']', open + 1);
        if (close == std::string_view::npos)
            return;

        auto value = trim(token.substr(0, open));
        auto field = trim(token.substr(open + 1, close - open - 1));

        cb(field, value);
    };

    for (std::size_t i = 0; i < subj.size(); ++i) {
        if (subj[i] == '"') {
            in_quotes = !in_quotes;
        } else if (subj[i] == ';' && !in_quotes) {
            parse_token(subj.substr(token_begin, i - token_begin));
            token_begin = i + 1;
        }
    }

    if (token_begin <= subj.size()) {
        parse_token(subj.substr(token_begin));
    }
}

char get_mmt_level(std::string_view mmt_class, std::size_t level) {
    if (level >= mmt_class.length()) return 0;
    return mmt_class[level];
}

///qualifier value without the quotes and padding TRTH puts around some values
std::string_view unquote(std::string_view value) {
    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = trim(value.substr(1, value.size() - 2));
    }
    return value;
}

///trading phase of a trade from its MMT_CLASS, nullopt for a trade outside the lit order book
std::optional<TradeQualifier> classify_trade(std::string_view qualifiers) {
    std::string_view mmt_class;
    parse_qualifiers(qualifiers, [&](auto key, auto val){
        if (key == "MMT_CLASS") mmt_class = val;
    });
    //level 1: 1 = central limit order book, Nasdaq Nordic reports its auctions as 5
    char l1 = get_mmt_level(mmt_class, 0);
    if (l1 != '1' && l1 != '5') return std::nullopt;
    switch (get_mmt_level(mmt_class, 1)) {
        case '2': return TradeQualifier::continuous;
        case 'O': return TradeQualifier::opening_auction;
        case 'K':
        case 'C': return TradeQualifier::closing_auction;
        case 'I': return TradeQualifier::intraday_auction;
        case 'U': return TradeQualifier::unscheduled_auction;
        //P is Auction on Demand on Nasdaq Nordic - only AOD orders take part,
        //so regular orders cannot be matched at its price
        default: return std::nullopt;
    }
}

bool TRTHEventSource::read_row(TRTHRawSource::Data &data) {
    if (_has_next) {
        std::swap(data, _next);
        _has_next = false;
        return true;
    }
    return _raw_source.read(data);
}

bool TRTHEventSource::peek_row() {
    if (!_has_next) _has_next = _raw_source.read(_next);
    return _has_next;
}

bool TRTHEventSource::parse_auction(Auction &a) {
    if (_data.Price.empty()) return false;
    std::string_view inst_phase;
    parse_qualifiers(_data.Qualifiers, [&](auto key, auto val){
        if (key == "INST_PHASE") inst_phase = val;
    });
    a.price = Decimal::from_string(_data.Price);
    a.quantity = _data.Volume.empty()?0_dec:Decimal::from_string(_data.Volume);
    a.imbalance = 0;
    a.auction_type = quarkbot::AuctionType::unknown;
    if (!inst_phase.empty()) {
        if (inst_phase == "I") a.auction_type = quarkbot::AuctionType::intraday;
        else if (inst_phase == "U") a.auction_type = quarkbot::AuctionType::unscheduled;
        else if (inst_phase == "O") a.auction_type = quarkbot::AuctionType::opening;
        else if (inst_phase == "E") a.auction_type = quarkbot::AuctionType::closing;
        else return false;
        return true;
    }
    //without INST_PHASE the auction type and the imbalance side come in a quote
    //without prices that follows the Auction row(s) with the same timestamp
    while (peek_row()) {
        if (_next.RIC != _data.RIC || _next.Date_Time != _data.Date_Time) break;
        if (_next.Type == "Auction") {
            //the same indicative repeated
            if (_next.Price != _data.Price || _next.Volume != _data.Volume) break;
            _has_next = false;
            continue;
        }
        if (_next.Type != "Quote" || !_next.Bid_Price.empty() || !_next.Ask_Price.empty()) break;
        bool imb = false;
        std::string_view act_tp, side;
        parse_qualifiers(_next.Qualifiers, [&](auto key, auto val){
            if (key == "IMB_ACT_TP") {imb = true; act_tp = unquote(val);}
            else if (key == "IMB_SIDE") side = unquote(val);
        });
        if (!imb) break;
        _has_next = false;
        if (act_tp == "O") a.auction_type = quarkbot::AuctionType::opening;
        else if (act_tp == "C") a.auction_type = quarkbot::AuctionType::closing;
        //Auction on Demand - see classify_trade()
        else if (act_tp == "F") return false;
        if (side == "B") a.imbalance = 1;
        else if (side == "S") a.imbalance = -1;
        break;
    }
    return true;
}

bool TRTHEventSource::parse_trade(Trade &t) {
    if (_data.Price.empty()) return false;
    auto qualifier = classify_trade(_data.Qualifiers);
    if (!qualifier) return false;
    t.price = Decimal::from_string(_data.Price);
    t.size = _data.Volume.empty()?0_dec:Decimal::from_string(_data.Volume);
    t.qualifier = *qualifier;
    if (!is_auction_trade(t.qualifier)) return true;
    //one uncross arrives as one print per execution, a few ms apart. The venue
    //ran a single auction, so the prints make a single auction trade.
    while (peek_row()) {
        if (_next.RIC != _data.RIC || _next.Type != "Trade" || _next.Price.empty()) break;
        if (classify_trade(_next.Qualifiers) != qualifier) break;
        if (Decimal::from_string(_next.Price) != t.price) break;
        if (!_next.Volume.empty()) t.size += Decimal::from_string(_next.Volume);
        _has_next = false;
    }
    return true;
}

bool TRTHEventSource::operator()(BacktestEvent &ev) {
    if (_eof) return false;

    while (true)  {
        if (!read_row(_data)) {
            _eof = true;
            return false;
        }

        std::string dt = _data.Date_Time;
        if (!dt.empty() && dt.back() == 'Z') dt.pop_back();
        std::istringstream in{dt};
        std::chrono::sys_time<std::chrono::nanoseconds> tp;
        in >> std::chrono::parse("%FT%T", tp);
        if (in.fail()) continue;
        ev.time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(tp);
        ev.symbol = _data.RIC;
        if (_data.Type == "Auction") {
            Auction &a = ev.data.emplace<Auction>();
            if (!parse_auction(a)) continue;
            a.time = ev.time;
        } else if (_data.Type == "Quote") {
            Quote &q = ev.data.emplace<Quote>();
            if (_data.Ask_Price.empty() && _data.Bid_Price.empty()) continue;
            q.ask = _data.Ask_Price.empty()?0_dec:Decimal::from_string(_data.Ask_Price);
            q.bid = _data.Bid_Price.empty()?0_dec:Decimal::from_string(_data.Bid_Price);
            q.ask_size = _data.Ask_Size.empty()?0_dec:Decimal::from_string(_data.Ask_Size);
            q.bid_size = _data.Bid_Size.empty()?0_dec:Decimal::from_string(_data.Bid_Size);
            q.time =  ev.time;
        } else if (_data.Type == "Trade") {
            auto &t = ev.data.emplace<Trade>();
            if (!parse_trade(t)) continue;
            t.time = ev.time;
        } else {
            continue;
        }
        return true;
    }
}

}

