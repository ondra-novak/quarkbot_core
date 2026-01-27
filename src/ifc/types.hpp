#pragma once

#include "utils/fixed.hpp"
#include <chrono>
namespace quarkbot {


enum class Side {
    buy = 1,
    sell = -1,
    undetermined = 0
};

enum class InstrumentType {
    ///spot instrument, you exchange one asset to another
    spot,
    ///contract - underlying currency is used to hold margin and position loss
    contract,
    ///inverse contract - like contract, but pnl is calculated using inverse price formula
    inverse_contract
};

struct ContractInfo {
    InstrumentType type;
    double multiplier;      ///contract multiplier (amount * multiplier * price = turnover)
    double tick_scale;      ///price multiplier
};

struct Fill {
    ///internal fill identifier (don't need to be unique!)
    std::string id;
    ///time when fill happened
    std::chrono::system_clock::time_point time;
    ///contract information
    ContractInfo contract;
    ///fill side
    Side side;
    ///fill amount
    Fixed amount;
    ///fill price
    Fixed price;
    ///absolute fees in original currency
    double fees;
    ///conversion rate between original currency and contract currency
    /**
    formula: contract_currenct = fees * fee_rate;
     */
    double fee_rate;
};

};