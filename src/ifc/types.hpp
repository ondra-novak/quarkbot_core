#pragma once

#include "utils/decimal.hpp"
#include <chrono>
#include <cstdint>
namespace quarkbot {



enum class Side : int8_t{
    buy = 1,
    sell = -1,
    undetermined = 0
};

inline constexpr Side reverse(Side s) {
    return static_cast<Side>(-static_cast<int>(s));
}

enum class InstrumentType: int8_t {
    ///spot instrument, you exchange one asset to another
    spot,
    ///contract - underlying currency is used to hold margin and position loss
    contract,
    ///inverse contract - like contract, but pnl is calculated using inverse price formula
    inverse_contract
};

struct ContractInfo {
    InstrumentType type;
    Decimal multiplier;      ///contract multiplier (amount * multiplier * price = turnover)
    Decimal tick_scale;      ///price multiplier

    Decimal calc_pnl(Decimal open_price, Decimal close_price, Decimal amount) const {
        open_price *= tick_scale;
        close_price *= tick_scale;
        if (type == InstrumentType::spot || type == InstrumentType::contract) {
            return (close_price - open_price) * amount * multiplier;
        } else if (type == InstrumentType::inverse_contract) {
            return (reciprocal(open_price) - reciprocal(close_price)) * amount * multiplier;
        }
        return {};
    }
    Decimal calc_average_price(Decimal open_price, Decimal new_fill_price, Decimal cur_amount, Decimal new_amount) const {
        open_price *= tick_scale;
        new_fill_price *= tick_scale;
        auto total = cur_amount+new_amount;
        if (type == InstrumentType::inverse_contract) {
            return reciprocal(reciprocal(open_price) * cur_amount + reciprocal(new_fill_price)*new_amount)/total;
        } else {
            return (open_price * cur_amount +new_fill_price *new_amount)/total;
        }
    }

    ///Calculate turnover in pnl currency (for profit and margin calculation)
    Decimal calc_turnover_pnl_currency(Decimal price, Decimal quantity) const {
        if (type == InstrumentType::inverse_contract) return multiplier * quantity * reciprocal(price*tick_scale);
        else return multiplier * quantity * price * tick_scale;
    }
    ///Calculate turnover in quote currency - for statistics)
    Decimal calc_turnover_quote_currency(Decimal price, Decimal quantity) const {
        if (type == InstrumentType::inverse_contract) return quantity * multiplier;
        else return multiplier * quantity * price * tick_scale;
    }



};

enum class ExecutionReason : int8_t{
    ///normal strategy order
    strategy_order,
    ///probably manual order from UI (not matching order found)
    manual_order,
    ///liquidation event
    liquidation,
    ///ADL event
    adl,
    ///close because contract expiration (settlement)
    settlement,
    ///automatic rollover, closed position on this contract
    rollover_close,
    ///automatic rollover, open position on new contract
    rollover_open
};

struct Fill {
    ///internal fill identifier (don't need to be unique!)
    std::string id;
    ///name of order responsible for this fill
    std::string order_name;
    ///time when fill happened
    std::chrono::system_clock::time_point time;
    ///contract information
    ContractInfo contract;
    ///fill side
    Side side;
    ///Execution reason
    ExecutionReason reason;
    ///fill amount
    Decimal amount;
    ///fill price
    Decimal price;
    ///absolute fees in original currency
    Decimal fees;
    ///conversion rate between original currency and contract currency
    /**
    formula: contract_currenct = fees * fee_rate;
     */
    Decimal fee_rate;
};

struct Position {    
   
    Side side = Side::undetermined;
    Decimal amount = {};  
    Decimal open_price = {};

    Decimal update(const Fill &fill, const ContractInfo &contract) {
        return update(fill.side,fill.price, fill.amount, contract);
    }
    Decimal update(Side s, Decimal price, Decimal quantity, const ContractInfo &contract) {
        if (s == Side::undetermined) {
            amount = quantity;
            open_price = price;
            side = s;
            return 0;
        }
        if (side == s) {
            open_price = contract.calc_average_price(open_price, price,amount, quantity);
            return 0;
        } 

        Decimal closing = std::max(amount, quantity);
        Decimal pnl = contract.calc_pnl(open_price, price, closing) * static_cast<int>(side);
        if (closing < quantity) {
            open_price = price;
            side = s;
            amount = quantity - closing;            
        } else {
            side = Side::undetermined;
            open_price = amount = 0;
        }
        return pnl;
    }

    Decimal get_upnl(Decimal price, const ContractInfo &contract) {
        return contract.calc_pnl(open_price, price, amount) * static_cast<int>(side);
    }
    Decimal get_volume(const ContractInfo &contract) {
        return contract.calc_turnover_pnl_currency(open_price, amount);
    }
};

};