#pragma once

#include <cmath>
#include "common.h"
#include "exchange.h"
#include "decimal.h"

namespace trading_api {


enum class SubscriptionType {
    ticker,
    orderbook,
};

inline std::string_view to_string(SubscriptionType type) {
    switch (type) {
        case SubscriptionType::ticker: return "ticker";
        case SubscriptionType::orderbook: return "orderbook";
        default: return "unknown";
    }
}


class IInstrument {
public:


    enum class Type: unsigned char {
        ///unknown type (not tradable)
        unknown,
        ///classic spot, no leverage, no short
        spot,
        ///futures/contract, can be leverage, can short
        contract,
        ///inverted COIN-M contract, strategy must invert price to calculate PNL
        inverted_contract,
        ///quantum contract, there is fixed ratio between PNL and profit in target currency
        quantum_contract,
        ///cfd market / however hedge is disabled by default
        cfd
    };

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

        bool operator==(const InstrumentFillInfo &info) const = default;

        ///calculates PNL for given arguments
        /**
         * @param amount amount of traded
         * @param open open price
         * @param close close price
         * @return returned pnl
         */
        Decimal calc_pnl(Side side, Decimal amount, Decimal open, Decimal close) const {
            if (type == Type::inverted_contract) {
                return -amount*multiplier*Decimal(side)*(1_dec/open - 1_dec/close);
            } else {
                return amount*multiplier*Decimal(side)*(close - open);
            }
        }
        ///Calculate position value - base value to calculate initial margin
        /**
         * @param size position size
         * @param open_price opening price
         * @return value of position in account's currency
         */
        Decimal calc_value(Decimal size, Decimal open_price) const {
            if (type == Type::inverted_contract) {
                return size * multiplier * (1_dec/open_price);
            } else
                return size * multiplier * open_price;
        }

    };


    struct Config {
        Type type = Type::unknown;
        ///quotation price step
        Decimal tick_size = 1;
        ///amount step
        Decimal lot_size = 1;
        ///multipler between contract size and real size (ex: multipler = 1000: amount=2.3 ~ 2300 shares)
        Decimal lot_multiplier = 1;
        ///minimal allowed lot size
        Decimal min_size = 0;
        ///minimal allowed volume (amount * multipler * price)
        Decimal min_volume = 0;
        ///fixed quantum factor between calculated pnl a real profit - (ex: 0.0001 USDT -> XBT = +10000 USDT profit = +1 XBT)
        Decimal quantum_factor = 1;
        ///required margin (0.05 = 5% = 20x leverage)
        /** this value is always 1 for spot markets */
        Decimal initial_margin = 1;
        ///maintenance margin (0.025 = 2.5% = 40x leverage)
        /** this value is always 0 for spot markets (as there is no liquidation event) */
        Decimal maintenance_margin = 0;
        ///instrument is tradable (you can place orders)
        bool tradable = false;
        ///instrument can be shorted
        bool can_short = false;
    };


    constexpr virtual ~IInstrument() = default;

    ///retrieve instrument account
    /**
     * @return configuration is returned as reference to improve performance
     * in many places where configuration is used. The instrument must
     * allocate configuration inside its instance
     */
    virtual const Config &get_config() const = 0;

    ///Retrieve internal instrument ID
    virtual std::string get_id() const = 0;

    ///Retrieve instrument's label (user defined)
    virtual std::string get_label() const = 0;


    virtual std::string get_category() const = 0;

    virtual Exchange get_exchange() const = 0;

    ///Retrieve instrument's fill information structure
    /** This helps to identify which fills belongs to which instruments */
    virtual InstrumentFillInfo get_fill_info() const = 0;

    class Null;
};


class IInstrument::Null: public IInstrument {
public:

    static constexpr Config null_config = {};

    virtual const IInstrument::Config &get_config() const override {
        return null_config;
    }
    virtual std::string get_id() const override {return {};}
    virtual std::string get_label() const override {return {};}
    virtual std::string get_category() const override {return {};}
    virtual Exchange get_exchange() const override {return {};}
    virtual InstrumentFillInfo get_fill_info() const override {return {};}


};

class Instrument: public Wrapper<IInstrument> {
public:
    using Config = IInstrument::Config;
    using Type = IInstrument::Type;
    using InstrumentFillInfo = IInstrument::InstrumentFillInfo;


    using Wrapper<IInstrument>::Wrapper;


    const Config &get_config() const {return _ptr->get_config();}

    ///Retrieve internal instrument ID
    /**Instrument internal ID is used to record instrument refernece
     * on the fill. You can use Instrument instance to retrieve fills
     * on given instrument. However, you cannot convert ID to Instrument.
     * @return
     */
    std::string get_id() const {
        return _ptr->get_id();
    }

    ///Retrieve label
    /**
     * The label is identificator provided by the user. It can specify
     * purpose of this instrument in context of the strategy
     * (for example "main", "hedge", etc)
     *
     * @return label
     */
    std::string get_label() const {
        return _ptr->get_label();
    }

    ///Instrument category - human readable text (for UI - optional)
    std::string get_category() const {return _ptr->get_category();}


    ///Retrieve exchange instance, where this instrument is managed
    Exchange get_exchange() const {return _ptr->get_exchange();}

    ///converts lot to amount
    /**
     * Instrument can present amount of shares in lots. This function converts
     * lots to real amount.
     *
     * Example: 1 lot is 100 shares. So reported position is 1.23 ~ amount = 123 shares
     *
     * @param lots reported lots
     * @return real amount
     */

    Decimal lot_to_amount(Decimal lots) const {
        return lot_to_amount(get_config(), lots);
    }

    ///converts real amount to lots
    /**
     * Instrument can present amount of shares in lots. This function converts
     * real amount to lots
     *
     * Example: 1 lot is 100 shares. You want to buy 123 shares, you need to buy
     * 1.23 lots
     *
     * @param amount real amount
     * @return lots
     */
    Decimal amount_to_lot(Decimal amount) const{
        return amount_to_lot(get_config(), amount);
    }

    ///converts quotation price to real price
    /**
     * quotation price can be different than real price. The real price can
     * be used to calculate profit or loss in equation
     *
     * real_amount * (real_close - real_open)
     *
     * This function return real price for quotation price
     *
     * Example: quantum contract defines a quantu 0.001. The quotation for the
     * contract is 147321. Real price for the contract is 147.321
     *
     * @param price quotation price
     * @return real price
     */
    Decimal quotation_to_price(Decimal price) const {
        return quotation_to_price(get_config(), price);
    }
    ///convert real price to quotation price
    /**
     * quotation price can be different than real price. The real price can
     * be used to calculate profit or loss in equation
     *
     * real_amount * (real_close - real_open)
     *
     * This function return quotation price for a real price
     *
     * Example: quantum contract defines a quantu 0.001. The real price of
     * this contract is 147.321. Quotation price is 147321
     *
     * @param price real price
     * @return quotation price
     */
    Decimal price_to_quotation(Decimal price) const {
        return price_to_quotation(get_config(), price);
    }

    ///adjust price to nearest tick
    /**
     * @param price price
     * @return adjusted price
     */
    Decimal adjust_price(Decimal price) const {
        return adjust_price(get_config(), price);
    }
    ///adjust amount to nearest lot
    /**
     * @param amount amount in lots
     * @return adjusted amount in lots
     */
    Decimal adjust_lot(Decimal amount) const {
        return adjust_lot(get_config(), amount);
    }
    Decimal adjust_lot_down(Decimal amount) const {
        return adjust_lot_down(get_config(), amount);
    }
    ///calculate minimal real amount for given price
    /**
     * @param price quotation price
     * @return minimal real amount (to_real_position)
     */
    Decimal calc_min_amount(Decimal price) const {
        return calc_min_amount(get_config(), price);
    }

    Decimal adjust_amount(Decimal price, Decimal size, bool size_is_volume) const {
        return adjust_amount(get_config(), price, size, size_is_volume);
    }
    static Decimal lot_to_amount(const Config &cfg, Decimal lots) {
        if (cfg.type == Type::inverted_contract) {
             lots = -lots;
        }
        return lots * cfg.lot_multiplier;
    }
    static Decimal amount_to_lot(const Config &cfg, Decimal amount) {
        amount=amount/cfg.lot_multiplier;
        if (cfg.type == Type::inverted_contract) {
             amount = -amount;
        }
        return amount;
    }
    static Decimal quotation_to_price(const Config &cfg, Decimal price) {
        switch (cfg.type){
            case Type::inverted_contract: return 1.0/price;
            case Type::quantum_contract: return price * cfg.quantum_factor;
            default: return price;
        }
    }
    static Decimal price_to_quotation(const Config &cfg, Decimal price) {
        switch (cfg.type){
            case Type::inverted_contract: return 1.0/price;
            case Type::quantum_contract: return price / cfg.quantum_factor;
            default: return price;
        }
    }
    static Decimal adjust_price(const Instrument::Config &cfg, Decimal price) {
        return std::max(round(price/cfg.tick_size)*cfg.tick_size, cfg.tick_size);
    }
    static Decimal adjust_lot(const Config &cfg, Decimal amount)  {
        return round(amount/cfg.lot_size)*cfg.lot_size;
    }
    static Decimal adjust_lot_down(const Config &cfg, Decimal amount)  {
        return floor(amount/cfg.lot_size)*cfg.lot_size;
    }
    static Decimal adjust_lot_up(const Config &cfg, Decimal amount)  {
        return ceil(amount/cfg.lot_size)*cfg.lot_size;
    }
    static Decimal calc_min_amount(const Config &cfg, Decimal price) {
        Decimal real_min_size = abs(lot_to_amount(cfg, cfg.min_size));
        Decimal real_lot_size = abs(lot_to_amount(cfg, cfg.lot_size));
        Decimal real_min_vol = abs(cfg.min_volume/ quotation_to_price(cfg, price));
        return std::max(std::max(real_min_size, real_lot_size), real_min_vol);
    }
    static Decimal calc_margin(const Config &cfg, Decimal price, Decimal amount, Decimal leverage) {
        Decimal real_amount = lot_to_amount(cfg, amount);
        Decimal real_price = quotation_to_price(cfg, price);
        return real_amount * real_price / leverage;
    }

    static Decimal adjust_amount(const Instrument::Config &cfg, Decimal price, Decimal size, bool size_is_volume) {
        if (size_is_volume) {
            Decimal ms = calc_min_amount(cfg, price);
            Decimal lt = adjust_lot_down(cfg,amount_to_lot(cfg, size/quotation_to_price(cfg, price)));
            if (ms > lot_to_amount(cfg, lt)) return 0;
            return lt;
        } else {
            Decimal ms = adjust_lot_up(cfg,amount_to_lot(cfg, calc_min_amount(cfg, price)));
            return std::max(ms,adjust_lot(cfg, size));
        }
    }

    InstrumentFillInfo get_fill_info() const {
        return _ptr->get_fill_info();
    }


};




inline Decimal price_instrument_to_strategy(const Instrument::Config &cfg, Decimal price) {
    switch (cfg.type) {
        default: return price;
        case Instrument::Type::inverted_contract: return 1.0/price;
    }
}

}

