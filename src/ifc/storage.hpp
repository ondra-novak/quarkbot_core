#pragma once

#include "../coro/src/basic_coro/awaitable.hpp"
#include "defs.hpp"
#include "market_instrument.hpp"

#include <chrono>
#include <span>
#include <string>
#include <string_view>

namespace quarkbot {



class IStorage {
public:
    virtual ~IStorage() = default;

    // ORDER STORAGE
    /*
        Stores references to active orders in the database.
        Storage is performed by the adapter for the given exchange in a format it understands.
        Each record contains:
            - exchange_id = exchange identifier, key under which orders are stored
            - order_id = order identifier - unique key
            - order_data = any binary information containing data needed to restore the order after restart

        The exchange adapter can then retrieve the stored information and compare it with the state on the exchange,
        and if necessary, deliver all events that occurred in the meantime
    */
    ///Store order information
    /**
    @param exchange_id exchange identifier
    @param order_id order identifier
    @param order_data binary data representing order state
     */
    virtual void store_order(std::string_view exchange_id, std::string_view order_id, std::string_view order_data) = 0;
    ///Delete order information - when order is closed, canceled, etc.
    /**
        @param exchange_id exchange identifier
        @param order_id order identifier
    */
    virtual void delete_order(std::string_view exchange_id, std::string_view order_id) = 0; 
    ///Count stored orders for specific exchange
    /**
    @param exchange_id exchange identifier
    @return number of stored orders (awaitable)
     */
    virtual coro::awaitable<std::size_t> count_orders(std::string_view exchange_id) = 0;
    ///Load stored orders for specific exchange
    /**
    @param exchange_id exchange identifier
    @param space space to load orders into. Each record is pair of order_id and order_data
    @return span containing actually loaded orders. It uses the provided space
     */
    virtual coro::awaitable<std::span<const std::pair<std::string,std::string> > > load_orders(std::string_view exchange_id, std::span<std::pair<std::string, std::string > > space) = 0;



    struct Fill {
        ///fill id
        std::string id;
        ///instrument identifier as returned by IMarketInstrument
        std::string instrument;
        ///ISO of fee curreny
        std::string fee_currency_iso;
        ///ID of position (created or closed) - useful for CFD
        std::string position_id;
        ///time of fill
        std::chrono::system_clock::time_point time;
        ///price of fill
        double price;
        ///size of fill - in contracts
        double size;
        ///information about fill context
        IMarketInstrument::FillInfo info;
        ///absolute value of fee
        double fee;        
        ///total funding added or removed
        double funding;
    };

    struct TradingState {
        ///contains time of first fill not included into calculation
        std::chrono::system_clock::time_point until_time = {};
        ///realized profit and loss
        double realized_pnl = 0.0;
        ///current position
        double position = 0.0;
        ///open price of current position (to calculate unrealized PNL)
        double open_price = 0.0;
        ///total funding
        double funding = 0.0;
    };

    struct FeeInfo {
        std::string iso;
        double fee;
    };

    struct FeeState {
        ///contains time of first fill not included into calculation
        std::chrono::system_clock::time_point until_time = {};
        
        std::vector<FeeInfo> fees = {};
    };


    // FILL STORAGE
    ///Store new fill
    /**
    @param fill fill information
     */
    virtual void store_fill(const Fill &fill) = 0;
    ///Retrieve last N fills for specific instrument
    /**
    @param space space to store fills.
    @note only fills for current strategy are returned
    @return span containing actually retrieved fills
    */
    virtual coro::awaitable<std::span<const Fill> > load_fills(std::string_view instrument_id, std::span<Fill> space) = 0;

    ///Calculate aggregated information about fills for specific instrument up to specific time
    /**
    @param instrument_id instrument unique identifier
    @param time time up to which PNL is calculated. If time is max(), all fills are considered
    @return PNL information (realized and unrealized)
     */
    virtual coro::awaitable<TradingState> aggregate_fills(std::string_view instrument_id, std::chrono::system_clock::time_point until_time, const TradingState &initial_state) = 0;

    virtual coro::awaitable<FeeState> aggregate_fees(std::string_view instrument_id, std::chrono::system_clock::time_point until_time, const FeeState &initial_state) = 0;


    // USER STRATEGY STORAGE
    ///Put user strategy key-value pair
    virtual void put(std::string_view key, std::string_view value) = 0;
    ///Get user strategy value by key
    virtual coro::awaitable<std::string> get(std::string_view key) = 0;
    ///Get count of keys with specific prefix
    virtual coro::awaitable<std::size_t> key_range(std::string_view key) = 0;
    ///Get key-value pairs with specific prefix
    virtual coro::awaitable<std::span<const std::pair<std::string, std::string> > > key_range(std::string_view key, std::span<std::pair<std::string, std::string> > space) = 0;
    ///Erase key-value pair by key
    virtual void erase(std::string_view key) = 0;
};

};