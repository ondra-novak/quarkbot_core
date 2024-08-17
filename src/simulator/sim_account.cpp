/*
 * sim_account.cpp
 *
 *  Created on: 16. 8. 2024
 *      Author: ondra
 */

#include "sim_account.h"

namespace trading_api {

SimAccount::SimAccount(Exchange exch, std::string label, std::string currency, double balance, double fees)
        :_exch(std::move(exch))
        ,_label(std::move(label))
        ,_currency(std::move(currency))
        ,_initial_balance(balance)
        ,_fees(fees)
        {}


std::string SimAccount::get_label() const {
    return _label;
}

Exchange SimAccount::get_exchange() const {
    return _exch;
}

SimAccount::Status SimAccount::get_status() const {
    std::shared_lock _(_mx);
    double eq = _initial_balance+_rpnl;
    auto mg = calc_position_stats();
    return {
        eq + mg.upnl,
        eq,
        mg.initial,
        mg.maintenance,
        mg.val / (eq+mg.upnl),
        _currency,
    };
}

std::string SimAccount::get_id() const {
    return _label;
}

SimAccount::Positions SimAccount::get_positions(const Instrument &i) const {
    std::shared_lock _(_mx);
    auto iter = _instrument_map.find(i);
    if (iter != _instrument_map.end()) {
        return iter->second;
    } else {
        return {};
    }
}

Fills SimAccount::create_fills(const Instrument &i, Side side, Decimal amount, Decimal price, Timestamp tm) {
    std::unique_lock _(_mx);
    Fills fills;
    auto fnfo = InstrumentFillInfo::from_instrument(i);
    auto &pslst = _instrument_map[i];
    Decimal remain_pos = amount;
    auto close_side = reverse(side);
    auto iter = std::remove_if(pslst.begin(), pslst.end(), [&](const Position &pos) {
       if (pos.side == close_side && pos.amount >= remain_pos) {
           remain_pos -= pos.amount;
           auto val = fnfo.calc_value(price, pos.amount);
           fills.push_back(Fill{
               tm,
               generate_pos_id(),
               {},
               pos.id,
               fnfo,
               side,
               pos.amount,
               price,
               _fees * val.as<double>()
           });
           _rpnl += fnfo.calc_pnl(pos.side, pos.amount, pos.open_price, price).as<double>();
           return true;
       } else {
           return false;
       }
    });
    pslst.erase(iter, pslst.end());
    if (remain_pos > 0_dec) {
        iter = std::find_if(pslst.begin(), pslst.end(), [&](const Position &pos){
            return pos.side == close_side;
        });
        if (iter != pslst.end()) {
            auto val = fnfo.calc_value(price, remain_pos);
            fills.push_back(Fill{
                tm,
                generate_pos_id(),
                {},
                iter->id,
                fnfo,
                side,
                remain_pos,
                price,
                _fees * val.as<double>()
            });
            iter->amount -= remain_pos;
            _rpnl += fnfo.calc_pnl(iter->side, remain_pos, iter->open_price, price).as<double>();
        } else {
            auto id = generate_pos_id();
            auto val = fnfo.calc_value(price, remain_pos);
            const auto &cfg = i.get_config();
            fills.push_back(Fill{
                tm,
                id,
                {},
                id,
                fnfo,
                side,
                remain_pos,
                price,
                _fees * val.as<double>()
            });
            pslst.push_back(Position{
                id,side,price,remain_pos, val*cfg.initial_margin, val*cfg.maintenance_margin
            });
        }
    }
    return fills;
}

std::optional<Fill> SimAccount::close_position(const Instrument &i, std::string id,
        Decimal price, Timestamp tm, Decimal remain) {
    std::unique_lock _(_mx);
    std::optional<Fill> out;
    auto fnfo = InstrumentFillInfo::from_instrument(i);
    auto &pslst = _instrument_map[i];
    auto iter = std::find_if(pslst.begin(), pslst.end(), [&](const Position &pos){
        return pos.id == id;
    });
    if (iter != pslst.end()) {
        Decimal to_close = iter->amount - remain;
        if (to_close > 0 && remain >= 0) {
            _rpnl += fnfo.calc_pnl(iter->side, to_close, iter->open_price, price).as<double>();
            out.emplace(Fill{
                tm,generate_pos_id(), {}, id, fnfo, iter->side, iter->amount, price,
                        _fees * (price * iter->amount).as<double>()
            });
            if (remain) iter->amount = remain;
            else pslst.erase(iter);
        }

    }
    return out;
}

std::string SimAccount::generate_pos_id() {
    static std::atomic<std::uint64_t> counter = 0;
    std::chrono::system_clock::time_point tm = std::chrono::system_clock::now();
    std::ostringstream buff;
    auto n = ++counter;
    buff << std::hex << n << 'x' << tm.time_since_epoch().count();
    return buff.str();
}

Fill SimAccount::open_position(const Instrument &i,
        Side side, Decimal price, Decimal size, Timestamp tm) {
    auto fnfo = InstrumentFillInfo::from_instrument(i);
    Fill out {
        tm,
        generate_pos_id(),
        {},
        generate_pos_id(),
        fnfo,
        side,
        size,
        price,
        _fees * (price * size).as<double>()
    };
    const auto &cfg = i.get_config();
    auto value = fnfo.calc_value(size, price);
    _instrument_map[i].push_back(Position{
        out.pos_id, side, price, size, cfg.initial_margin* value, cfg.maintenance_margin * value
    });
    return out;
}

SimAccount::PositionStats SimAccount::calc_position_stats() const {
    PositionStats mi = {};
    for (const auto &[i, pslst]: _instrument_map) {
        const auto &cfg = i.get_config();
        for (const auto &pos: pslst) {
            mi.initial += pos.initial_margin.as<double>();
            mi.maintenance += pos.maintenance_margin.as<double>();
            mi.val += (pos.initial_margin/cfg.initial_margin).as<double>();
        }
    }
    return mi;
}


} /* namespace trading_api */
