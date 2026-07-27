#pragma once

#include "decimal.hpp"
#include "utils/lookup.hpp"
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <tuple>
namespace quarkbot {

template<typename T> struct FromString;

template<typename T>
inline constexpr std::nullptr_t string_lookup = {};
template<typename T>
inline constexpr bool assert_false = false;

template<typename T>
concept HasFromStringMethod = requires(std::string_view v) {
    {T::from_string(v)} -> std::same_as<T>;
};

template<typename T>
concept HasStringLookup =  !std::same_as<std::remove_cvref_t<decltype(string_lookup<T>)>, std::nullptr_t> && requires(T val) {
    {string_lookup<T>(val)} -> std::same_as<std::optional<std::string_view>  >;
};

enum class Side : int8_t{
    buy = 1,
    sell = -1,
    undetermined = 0
};

template<>
inline constexpr auto string_lookup<Side> = make_lookup_table<Side,std::string_view>({
    {Side::buy, "buy"},
    {Side::sell, "sell"},        
});

static_assert(HasStringLookup<Side>);

inline constexpr Side reverse(Side s) {
    return static_cast<Side>(-static_cast<int>(s));
}

enum class InstrumentType: int8_t {
    ///spot instrument, you exchange one asset to another
    spot,
    ///trading on spot, but can be leveraged, underlying currency is used to hold margin and position loss
    margin,
    ///contract - underlying currency is used to hold margin and position loss (futures)
    contract,
    ///inverse contract - like contract, but pnl is calculated using inverse price formula
    inverse_contract
};

template<>
inline constexpr auto string_lookup<InstrumentType> = make_lookup_table<InstrumentType,std::string_view>({
    {InstrumentType::spot,"spot"},
    {InstrumentType::margin,"margin"},
    {InstrumentType::contract,"contract"},
    {InstrumentType::inverse_contract,"inverse_contract"},
    });

struct ContractInfo {
    InstrumentType type = InstrumentType::contract;
    Decimal multiplier = 1;      ///contract multiplier (amount * multiplier * price = turnover)
    Decimal tick_scale = 1;      ///price multiplier

    bool operator==(const ContractInfo &) const = default;

    void serialize(this auto &self, auto &ar) {
        ar(self.type,"type");
        ar(self.multiplier,"multiplier");
        ar(self.tick_scale,"tick_scale");
    }

    template<typename Self>
    auto fields(this Self &self) {
        return std::tie(self.type, self.multiplier, self.tick_scale);
    }


    Decimal calc_pnl(Decimal open_price, Decimal close_price, Decimal amount) const {
        open_price *= tick_scale;
        close_price *= tick_scale;
        if (type == InstrumentType::spot || type == InstrumentType::contract) {
            return (close_price - open_price) * amount * multiplier;
        } else if (type == InstrumentType::inverse_contract && amount) {
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

template<>
inline constexpr auto string_lookup<ExecutionReason> = make_lookup_table<ExecutionReason,std::string_view>({
    {ExecutionReason::strategy_order,"strategy_order"},
    {ExecutionReason::manual_order,"manual_order"},
    {ExecutionReason::liquidation,"liquidation"},
    {ExecutionReason::adl,"adl"},
    {ExecutionReason::settlement,"settlement"},
    {ExecutionReason::rollover_close,"rollover_close"},
    {ExecutionReason::rollover_open,"rollover_open"},
});




///declaration of generic record key
/**
In key-value database, there are record with following key format:
<keyspaceId><prefix><ordered><random>
*/
struct RecordKey {
    ///keys are ordered by this number - for example timestamp
    std::uint64_t ordered;
    ///if two keys share same ordered value, this number must be different (random)
    std::uint64_t random;

    constexpr bool operator==(const RecordKey &) const = default;
    constexpr std::strong_ordering operator<=>(const RecordKey &) const = default;

    static constexpr RecordKey min() {
        return {0,0};
    } 
    static constexpr RecordKey max() {
        auto mx = std::numeric_limits<std::uint64_t>::max();
        return {mx,mx};
    }

};



struct Fill {
    ///Record key of this fill generated by exchange adapter. It is not serialized
    RecordKey key;
    ///internal fill identifier (don't need to be unique!)
    std::string id;
    ///label of order responsible for this fill
    std::string label;
    ///time when fill happened
    std::chrono::system_clock::time_point time;
    ///contract information
    ContractInfo contract;
    ///fill side
    Side side;
    ///Execution reason
    ExecutionReason reason;
    ///fill amount
    Decimal quantity;
    ///fill price
    Decimal price;

    bool operator==(const Fill &other) const = default;

    void serialize(this auto &self, auto &ar) {
        ar(self.id,"id");
        ar(self.label,"label");
        ar(self.time,"time");
        ar(self.contract,"contract");
        ar(self.side,"side");
        ar(self.reason,"reason");
        ar(self.quantity,"quantity");
        ar(self.price,"price");
    }
    
};

struct Position {    
   
    Side side = Side::undetermined;
    Decimal amount = {};  
    Decimal open_price = {};

    Decimal update(const Fill &fill, const ContractInfo &contract) {
        return update(fill.side,fill.price, fill.quantity, contract);
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

struct WalletInfo {
    ///available balance (can be used for trading)
    Decimal balance = {};
    ///unrealized pnl for open positions in this currency (if applicable)
    Decimal unrealized_pnl = {};
    ///blocked balance for opened orders (spot markets)
    Decimal order_blocked = {};
    ///initial margin for opened orders and positions (leveraged markets)
    Decimal initial_margin = {};
    ///maintenance margin for opened positions (leveraged markets)
    Decimal maintenance_margin = {};

    Decimal remaining_balance() const {
        return balance + unrealized_pnl - order_blocked - initial_margin;
    }
};


class UninitializedException: public std::runtime_error {
public:
    UninitializedException():std::runtime_error("Called a method on uninitialized variable") {}
};

template<typename T>
concept MarketInstrumentStream = requires {
    typename T::MarketInstrumentStream;
};


};