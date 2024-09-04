#pragma once

#include <quarkbot/exchange.h>
#include <shared_mutex>
namespace quarkbot {

class SimAccount: public IAccount {
public:
    SimAccount(ExchangeInfo exch, std::string label, std::string currency, double balance, double fees);
    virtual std::string get_label() const override;
    virtual ExchangeInfo get_exchange() const override;
    virtual Status get_status() const override;
    virtual std::string get_id() const override;
    virtual Positions get_positions(const Instrument &i) const override;
    virtual double get_ratio(const Instrument &) const override {return 0;}

    Fills create_fills(const Instrument &i, Side side, Decimal amount, Decimal price, Timestamp tm, std::string_view label);
    std::optional<Fill> close_position(const Instrument &i, std::string id, Decimal bid, Decimal ask, Timestamp tm, std::string_view label, Decimal remain = 0_dec);
    Fill open_position(const Instrument &i, Side side, Decimal price, Decimal size, Timestamp tm, std::string_view label);
    Decimal get_max_reduce(const Instrument &i, Side side);

    static std::string generate_uid();

    static SimAccount &from_account(const Account &a);

protected:

    ExchangeInfo _exch;
    std::string _label;
    std::string _currency;
    double _initial_balance;
    double _fees;


    mutable std::shared_mutex _mx;
    std::unordered_map<Instrument, Positions, Instrument::Hasher> _instrument_map;
    double _rpnl = 0;



    struct PositionStats {
        //initial margin from all positions
        double initial = 0;
        //maintenance margin from all positions
        double maintenance = 0;
        //total held value
        double val = 0;
        //unrealized PNL
        double upnl = 0;
    };

    PositionStats calc_position_stats() const;


};

}
