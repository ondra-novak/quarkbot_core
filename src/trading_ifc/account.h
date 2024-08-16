#pragma once

#include "common.h"
#include "acb.h"
#include <vector>
#include <memory>
#include <chrono>
#include <string_view>
#include "instrument.h"


namespace trading_api {



class Instrument;

class IAccount {
public:
    struct Info {
        ///Total equity : current balance + unrealized pnl
        double equity = 0;  //balance + upnl
        ///Current total balance
        /** For leveraged markets, it contains available balance + initial margin
         * for all positions, but doesn't includes unrealized pnl
         * For spot markets, it contains available balance + value of all associated
         * assets. This is reported as positions with leverage = 1, so initial margin
         * is equal to position's value
         */
        double balance = 0;
        ///Total initial margin
        /** Sum of initial margins of all opened positions
         *
         * For spot market, leverage is set 1, so initial margin is equal to value
         * of all positions
         * */
        double initial_margin = 0;  //balance blocked for initial_margin
        ///Total maintenance margin
        /**
         * Contains number which defines point of liquidation if equity falls below
         * this number. It is calculated as sum of maintenance margin for all opened
         * positions
         */
        double maintenance_margin = 0;  //liquidation level
        ///Current leverage
        /**
         * contains ratio between total opened value and equity
         * total_value/equity
         */
        double leverage = 0;            //min common leverage, if zero - unknown
        ///Contains currency string, for example USD
        std::string currency = {};
        ///Contains ratio to master currency, if set to 0, then ratio is unknown or there is no master currency
        double ratio = 0;
    };


    struct Position {
        ///Position identifier - optional string
        /**
         * Some instruments can support multiple positions. In this case, each position
         * is identified by its ID. You can use ID to close or reduce position. If the
         * instrument tracks only one position, this ID is often empty string. However
         * you can still use this ID to control the position
         *
         * If the future instrument supports hedge positions, it probably can
         * report two opened positions, one for LONG side and one for SHORT side
         */
        std::string id;
        ///Position side
        /**
         * If value is side buy = LONG, side sell = SHORT
         */
        Side side;
        ///Opening price or average opening proce (ACB)
        Decimal open_price;
        ///Position size
        Decimal amount;
        ///Initial margin for this position
        Decimal initial_margin;
        ///Maintenance margin for this position
        Decimal maintenance_margin;
    };

    struct AggregatedPosition {
        ///final side of the aggregated position
        Side side = Side::undefined;
        ///open price of this position
        Decimal open_price = 0;
        ///amount of position
        Decimal amount = {};
        ///locked pnl - nonzero if hedge positions
        Decimal locked_pnl = 0;
    };

    class Positions : public std::vector<Position>{
    public:
        using std::vector<Position>::vector;

        ///aggregates all positions into one
        /**
         *
         * @tparam skip specifies which side to skip (Side::buy -> Side::sell is used)
         *         default value processes all positions
         * @param i associated instrument (we need original instrument to calculate some
         *         fields, as the instrument is not part of positions)
         * @return aggregated position
         */
        template<Side skip = Side::undefined>
        AggregatedPosition aggregated(const Instrument &i)const {
            auto finfo = i.get_fill_info();
            AggregatedPosition out;
            for (const Position &pos: *this) {
                if (pos.side == skip) continue;
                if (pos.side == out.side) {
                    out.open_price = (out.open_price * out.amount
                            + pos.open_price * pos.amount) /
                                    (out.amount + pos.amount);
                    out.amount += pos.amount;
                } else if (pos.amount <= out.amount) {
                    out.locked_pnl += finfo.calc_pnl(out.side,pos.amount, out.open_price, pos.open_price);
                    out.amount -= out.amount;
                    if (out.amount == 0_dec) {
                        out.side = Side::undefined;
                    }
                } else {
                    out.locked_pnl += finfo.calc_pnl(out.side,out.amount, out.open_price, pos.open_price);
                    out.amount = pos.amount - out.amount;
                    out.side = pos.side;
                }
            }
            return out;
        }
        ///aggregate all buy positions (in hedge mode)
        AggregatedPosition aggregated_buy(const Instrument &i) const {return aggregated<Side::sell>(i);}
        ///aggregate all sell positions (in buy mode)
        AggregatedPosition aggregated_sell(const Instrument &i) const {return aggregated<Side::buy>(i);}
    };

    virtual ~IAccount() = default;


    virtual Info get_info() const = 0;
    virtual std::string get_label() const = 0;

    virtual Exchange get_exchange() const = 0;

    ///Retrieve internal instrument ID
    virtual std::string get_id() const = 0;

    virtual Positions get_positions(const Instrument &i) const = 0;


    class Null;
};


class IAccount::Null: public IAccount{
public:
    virtual Info get_info() const override {return {};}
    virtual std::string get_label() const override {return {};}
    virtual Exchange get_exchange() const override {return {};}
    virtual std::string get_id() const override {return {};}
    virtual Positions get_positions(const Instrument &) const override {return {};}
};



///the account on which the particulal symbol is traded
/**
 * @note the account can be part of user account on the exchange. On
 * spot exchange, the account is mapped on symbol which represents
 * currency side. On futures account, this can be mapped to collateral
 * account type. One user account can have multiple such accounts.
 */
class Account: public Wrapper<IAccount> {
public:

    using Wrapper<IAccount>::Wrapper;

    using Info = IAccount::Info;
    using Position = IAccount::Position;
    using Positions = IAccount::Positions;
    using AggregatedPosition = IAccount::AggregatedPosition;


    ///Retrieve account's label
    /** Account's label can be defined in config */
    std::string get_label() const {return _ptr->get_label();}


    Info get_info() const {return _ptr->get_info();}

    ///Retrieve exchange instance, where this account is managed
    Exchange get_exchange() const {return _ptr->get_exchange();}

    std::string get_id() const {return _ptr->get_id();}

    Positions get_positions(const Instrument &i) const {return _ptr->get_positions(i);}

};



}
