#pragma once

#include <unordered_map>


namespace trading_api {

struct PositionInfo {
    Side side = Side::undefined;      //current side
    Decimal pos = {};    //current position
    double sum = 0;
    Fill last_fill = {};
    double fees = 0;

    std::optional<Trade> add_fill(const Fill &f) {
        std::optional<Trade> out;
        last_fill = f;
        double fp;
        if (f.instrument.type == Instrument::Type::inverted_contract) {
            fp = 1.0/f.price.as<double>();
        } else {
            fp =f.price.as<double>();
        }
        fees += f.fees;
        if (f.side == side) {
            pos += f.amount;
            sum += f.amount.as<double>() * fp;;
        } else {
            if (pos <= f.amount) {
                out.emplace(Trade {
                    f.time,f.id, f.label, f.pos_id, f.instrument,side, pos,
                    get_open_price(), f.price.as<double>(), fees
                });
                pos = f.amount - pos;
                side = f.side;
                sum = pos.as<double>() * fp;
                fees = 0;
            } else {
                out.emplace(Trade {
                    f.time,f.id, f.label, f.pos_id, f.instrument,side, f.amount,
                    get_open_price(), f.price.as<double>(), fees
                });
                auto newpos = pos + f.amount;
                double newavg = sum * newpos.as<double>() / pos.as<double>();
                pos -= f.amount;
                sum = newavg;
            }
        }
        return {};
    }

    double get_open_price() const {
        double avg  = sum / pos.as<double>();
        if (last_fill.instrument.type == Instrument::Type::inverted_contract) {
            return 1.0/avg;
        } else {
            return avg;
        }
    }
};

class PositionInfoMap: public std::unordered_map<std::string, PositionInfo>{
public:

    auto export_open_positions(std::string_view filter) const {
        Positions res;
        for (const auto &[id, info]: *this) {
            if (info.pos == 0) {
                continue;
            }
            std::string_view f= info.last_fill.label;
            if (filter.empty() || f.substr(0,filter.size()) == filter) {
                res.push_back({
                    info.last_fill.time,
                    info.last_fill.id,
                    info.last_fill.label,
                    id,
                    info.last_fill.instrument,
                    info.side,
                    info.pos,
                    info.get_open_price(),
                    info.fees,
                });
            }
        }
        return res;
    }


};

}
