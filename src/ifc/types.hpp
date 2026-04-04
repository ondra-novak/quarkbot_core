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

};