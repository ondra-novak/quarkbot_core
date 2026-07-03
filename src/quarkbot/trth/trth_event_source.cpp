#include "trth_event_source.hpp" 
#include "quarkbot/abstract/backtest_data_source.hpp"
#include "quarkbot/utils/string_utils.hpp"
#include "trth_raw_source.hpp"
#include <chrono>
#include <memory>
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


bool TRTHEventSource::operator()(BacktestEvent &ev) {
    if (_eof) return false;

    while (true)  {
        if (!_raw_source.read(_data)) {
            _eof = true;   
            return false;         
        } 

        std::string dt = _data.Date_Time;
        if (!dt.empty() && dt.back() == 'Z') dt.pop_back();
        std::istringstream in{dt};
        std::chrono::sys_time<std::chrono::nanoseconds> tp;
        in >> std::chrono::parse("%FT%T", tp);
        if (in.fail()) continue;
        ev.time = tp;
        ev.symbol = _data.RIC;
        if (_data.Type == "Auction") {
            Auction &a = ev.data.emplace<Auction>();
            std::string_view inst_phase;
            parse_qualifiers(_data.Qualifiers, [&](auto key, auto val){
                if (key == "INST_PHASE") inst_phase = val;
            });
            if (inst_phase.empty()) a.auction_type = quarkbot::AuctionType::unknown;
            else if (inst_phase == "I") a.auction_type = quarkbot::AuctionType::intraday;
            else if (inst_phase == "U") a.auction_type = quarkbot::AuctionType::unscheduled;
            else if (inst_phase == "O") a.auction_type = quarkbot::AuctionType::opening;
            else if (inst_phase == "E") a.auction_type = quarkbot::AuctionType::closing;
            else continue;
            if (_data.Price.empty()) continue;
            a.final = false;
            a.imbalance = 0;
            a.price = Decimal::from_string(_data.Price);
            a.quantity = _data.Volume.empty()?0_dec:Decimal::from_string(_data.Volume);
            a.quantity_traded = 0;
            a.time = ev.time;
        } else if (_data.Type == "Quote") {
            Quote &q = ev.data.emplace<Quote>();
            if (_data.Ask_Price.empty() && _data.Bid_Price.empty()) return false;
            q.ask = _data.Ask_Price.empty()?0_dec:Decimal::from_string(_data.Ask_Price);
            q.bid = _data.Bid_Price.empty()?0_dec:Decimal::from_string(_data.Bid_Price);
            q.ask_size = _data.Ask_Size.empty()?0_dec:Decimal::from_string(_data.Ask_Size);
            q.bid_size = _data.Bid_Size.empty()?0_dec:Decimal::from_string(_data.Bid_Size);
            q.time =  ev.time;
        } else if (_data.Type == "Trade") {
            if (_data.Price.empty()) return false;
            std::string_view mmt_class;
            parse_qualifiers(_data.Qualifiers, [&](auto key, auto val){
                if (key == "MMT_CLASS") mmt_class = val;
            });
            quarkbot::AuctionType atype = {};
            if (!mmt_class.empty()) {
                char tm = get_mmt_level(mmt_class, 0);
                if (tm != '1') return false;                
                char l2 = get_mmt_level(mmt_class, 1);
                switch (l2) {
                    case '2': ;break;
                    case 'O': atype = quarkbot::AuctionType::opening;break;
                    case 'C': atype = quarkbot::AuctionType::closing;break;
                    case 'U': atype = quarkbot::AuctionType::unscheduled;break;
                    case 'I': atype = quarkbot::AuctionType::intraday;break;
                    default: return false;
                }            
            } else {
                continue;
            }
            if (atype == quarkbot::AuctionType::unknown) {
                auto &t = ev.data.emplace<Trade>();
                t.price = Decimal::from_string(_data.Price);
                t.size = _data.Volume.empty()?0_dec:Decimal::from_string(_data.Volume);
                t.time = ev.time;
            } else {
                auto &a = ev.data.emplace<Auction>();
                a.auction_type = atype;
                a.final = true;
                a.quantity = a.quantity_traded =  _data.Volume.empty()?0_dec:Decimal::from_string(_data.Volume);
                a.imbalance = 0;
                a.price = Decimal::from_string(_data.Price);
                a.time = ev.time;
            }        
        } else {
            continue;
        }
        return true;        
    }   
}


}

