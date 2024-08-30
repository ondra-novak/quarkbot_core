#pragma once

#include "common.h"
#include "timer.h"
#include "instrument.h"
#include "decimal.h"
#include <vector>
#include <string>

namespace quarkbot {


///Structure defines information of this instrument for fills
/** This structure contains sufficient informations to aggregate position, trades and calculate PnL */
struct InstrumentFillInfo {
    ///type of contract (to calculate PnL correcly)
    IInstrument::Type type;
    ///PnL multiplier (multiplier * amount * (close - open)
    Decimal multiplier;
    ///instrument identifier (to aggregate fills of single instrument)
    std::string instrument_id;
    ///price unit (for example USD)
    std::string price_unit;

    static InstrumentFillInfo from_instrument(const IInstrument::Config &cfg, std::string id) {
        return {
            cfg.type,cfg.lot_multiplier*cfg.quanto_factor, std::move(id), cfg.currency
        };
    }
    static InstrumentFillInfo from_instrument(const Instrument &i) {
        return from_instrument(i.get_config(), i.get_id());
    }


    bool operator==(const InstrumentFillInfo &info) const = default;

    ///calculates PNL for given arguments
    /**
     * @param amount amount of traded
     * @param open open price
     * @param close close price
     * @return returned pnl
     */
    template<typename _Float>
    _Float calc_pnl(Side side, _Float amount, _Float open, _Float close) const {
        return Instrument::calc_pnl<_Float>(type, static_cast<_Float>(multiplier), side, amount, open,close);
    }
    ///Calculate position value - base value to calculate initial margin
    /**
     * @param size position size
     * @param open_price opening price
     * @return value of position in account's currency
     */
    template<typename _Float>
    _Float calc_value(_Float size, _Float open_price) const {
        return Instrument::calc_value<_Float>(type, static_cast<_Float>(multiplier), size, open_price);
    }

};



///single fill
struct Fill {

    ///Fill timestamp (UTC)
    /**
     * Note this field is constant for given fill (with same fill_id).
     * If reported timestamp is different for the same fill_id, the fill
     *  can be reported twice.
     *
     */
    Timestamp time;

    ///exchange's unique ID
    std::string id;

    ///User defined label (for filtering)
    std::string label;

    ///Position identifier
    /** This is optional id
     * If the string is filled, id contains ID of position, which has been created, increased,
     * reduced or closed. The exchange is responsible to match correct information about positions
     * If the position doesn't exists, or amount of reduced is greater than size of position,
     * extra amount is not counted to overall position.
     * If the string is blank, fill is counted to global position.
     */
    std::string pos_id;

    InstrumentFillInfo instrument;

    ///execution side
    Side side;

    Decimal amount;
    ///Commision (fees)
    /**
     * Absolute amount of fees in instrument's currency. If
     * fees are subsctracted from amount, this value contains fees recalculated
     * to the currency (amount_fees * price)
     *
     */
    ///Price of this fill
    Decimal price;
    ///Amount exchanged
    /**
     * @note if fees are subtracted from the amount, this field is filled by
     * amount after substraction
     *
     */
    double fees;

    bool operator==(const Fill & other) const {
        return id == other.id;
    }

};


using Fills = std::vector<Fill>;

///single position (aggregated from fills)
struct Position {

    ///Position last update time
    Timestamp last_update_time;

    ///Position last_fill_id
    std::string last_fill_id;

    ///User defined label  - retrieves from last trade
    std::string label;

    ///Position identifier
    std::string pos_id;

    ///Information about instrument
    InstrumentFillInfo instrument;

    ///execution side
    /// for position it contains overall side
    Side side;

    ///Amount exchanged
    /**
     * @note if fees are subtracted from the amount, this field is filled by
     * amount after substraction
     *
     * for position it contains position amount
     */
    Decimal amount;

    ///average open price (ACB)
    double open_price;
    ///Commision (fees)
    /**
     * Absolute amount of fees in instrument's currency. If
     * fees are subsctracted from amount, this value contains fees recalculated
     * to the currency (amount_fees * price)
     *
     * for position it contains total fees
     */
    double fees;

    bool operator==(const Position & other) const {
        return pos_id == other.pos_id && instrument == other.instrument;
    }

};


using Positions = std::vector<Position>;

///single closed trade (aggregated from fills)
struct Trade {

    ///Position last update time
    Timestamp last_update_time;

    ///Position last_fill_id
    std::string last_fill_id;

    ///User defined label  - retrieves from last trade
    std::string label;

    ///associated position id (close end reduce)
    std::string pos_id;

    ///Information about instrument
    InstrumentFillInfo instrument;

    ///execution side
    /// for position it contains overall side
    Side side;

    ///Amount exchanged
    /**
     * @note if fees are subtracted from the amount, this field is filled by
     * amount after substraction
     *
     * for position it contains position amount
     */
    Decimal amount;

    ///average open price (ACB)
    double open_price;

    ///closing price
    double close_price;
    ///Commision (fees)
    /**
     * Absolute amount of fees in instrument's currency. If
     * fees are subsctracted from amount, this value contains fees recalculated
     * to the currency (amount_fees * price)
     *
     * for position it contains total fees
     */
    double fees;

    double calc_pnl() const {
        return instrument.calc_pnl<double>(side, amount.as<double>(), open_price, close_price);
    }

};

using Trades = std::vector<Trade>;

///Aggregated trades for single structure shows profit or loss on single instrument
struct ProfitLoss {

    ///information about instrument
    InstrumentFillInfo instrument;

    ///total pnl
    double pnl;

    ///total fees
    double fees;
};

using TradingStatistics = std::vector<ProfitLoss>;


///calculate statistics from trades
inline TradingStatistics calculate_statistics(const Trades &trades) {
    TradingStatistics stats;
    for (const auto &x: trades) {
        auto f = std::find_if(stats.begin(), stats.end(), [&](const ProfitLoss &pnl){
            return pnl.instrument == x.instrument;
        });
        if (f == stats.end()) {
            stats.push_back({x.instrument, x.calc_pnl(), x.fees});
        } else {
            f->pnl+=x.calc_pnl();
            f->fees+= x.fees;
        }
    }
    return stats;


}


}
