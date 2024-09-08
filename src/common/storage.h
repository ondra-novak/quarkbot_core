#pragma once

#include <quarkbot/strategy_context.h>
#include <quarkbot/account.h>
#include <unordered_set>
#include <list>

namespace quarkbot {

class IStorage {
public:


    virtual ~IStorage() = default;
    ///begin transaction - all put or erase are now stored into single transaction
    virtual void begin_transaction() = 0;
    ///put strategy custom variable
    /**
     * @param event_time current event time, associated with update. Some database
     * provider may want to store this information
     * @param name name of variable (expect a binary content)
     * @param value content of variable (expect a binary content)
     */
    virtual void put_var(Timestamp event_time, std::string_view name, std::string_view value) = 0;
    ///erase strategy custom variable
    /**
     * @param event_time current event time associated with the update. Some database
     * provider may want to store this information
     * @param name name of variable (expect a binary content)
     */
    virtual void erase_var(Timestamp event_time, std::string_view name) = 0;
    ///put order (called on every order update)
    /**
     * @param event_time current event time associated with the update. Some database
     * provider may want to store this information
     * @param ord order
     */
    virtual void put_order(Timestamp event_time, const Order &ord) = 0;
    ///put fill (called on fill)
    /**
     * @param event_time current event time associated with the update. Some database
     * provider may want to store this information
     * @param fill fill to store
     */
    virtual void put_fill(Timestamp event_time, const Fill &fill) = 0;

    /// Append a point to a series
    /**
     * @param series_name Name of the series
     * @param point_data Binary representation of a point (serialized to binary)
     * @return Index of the newly created point
     */
    virtual std::uint64_t series_add_point(std::string_view series_name, std::string_view point_data) = 0;

    /// Erase older points from a series
    /**
     * @param series_name Name of the series
     * @param index_and_less Highest index of points to erase
     */
    virtual void series_erase_points(std::string_view series_name, std::uint64_t index_and_less) = 0;

    ///commit all writes
    virtual void commit() = 0;
    ///discard writes
    virtual void rollback() = 0;
    ///determine, whether given fill is duplicate
    virtual bool is_duplicate_fill(const Fill &fill) const = 0;
    ///load recent fills
    /**
     * @param limit limit in count
     * @param flt_instrument filter for given instrument
     * @param flt_account filter for given account
     * @return fills
     */
    virtual Fills load_fills(std::size_t limit, std::string_view filter = {}) const = 0;
    ///load recent fills
    /**
     * @param limit limit as old timestamp. No older fills are returned
     * @param flt_instrument filter for given instrument
     * @param flt_account filter for given account
     * @return fills
     */
    virtual Fills load_fills(Timestamp limit, std::string_view filter = {}) const = 0;
    ///load all open orders (stored binary)
    /**
     * @param account account identifier. It expects, that account unique identifier
     * is preserved between runs.
     */
    virtual std::vector<SerializedOrder> load_open_orders(const Account &account) const = 0;
    ///get value of variable
    /**
     * @param var_name variable name
     * @return string content of variable. If variable is not defined, returns empty string
     */
    virtual std::string get_var(std::string_view var_name) const = 0;

    ///Retrieve set of variables
    /**
     * @param prefix name prefix. Function returns all variables starting with given prefix
     * @return iteratable variable set
     */
    virtual VarSet<std::string_view> get_vars(std::string_view prefix) const = 0;

    ///Retrieve set of variables
    /**
     * @param start begin of variables (included)
     * @param end end of variables (included)
     * @return iteratable variable set
     * @note start < end alphanumerical otherwise empty set can be returned
     *
     */
    virtual VarSet<std::string_view> get_vars(std::string_view start, std::string_view end) const = 0;

    ///load positions
    /**
     * Positions are aggregated from fills
     * @param filter filters positions by label
     * @return list of active positions
     */
    virtual Positions load_positions(std::string_view filter = {} ) const = 0;
    ///load closed trades
    /**
     * @param limit oldest timestamp
     * @param filter if specified it only returns trades with given label
     * @return trades
     */
    virtual Trades load_closed(Timestamp limit, std::string_view filter = {} ) const = 0;


    ///Loads points of series
    /**
     * @param name
     * @return instance of Values - iteratable container of values in
     * binary serialized format for given series
     */
    virtual ValueStream<std::string_view> load_series(std::string_view name) const = 0;

    class Null;

};

class IStorage::Null: public IStorage {
public:
    virtual void rollback() override {}
    virtual void begin_transaction() override {}
    virtual void put_order(Timestamp,const Order &) override {}
    virtual void erase_var(Timestamp,std::string_view ) override {}
    virtual void put_fill(Timestamp,const Fill &) override {}
    virtual void put_var(Timestamp, std::string_view , std::string_view ) override {}
    virtual void commit() override {}
    virtual bool is_duplicate_fill(const Fill &) const override {return false;} ;
    virtual std::vector<SerializedOrder> load_open_orders(const Account &) const override {return {};}
    virtual Fills load_fills(std::size_t, std::string_view) const override{return {};}
    virtual Fills load_fills(Timestamp ,std::string_view) const  override{return {};}
    virtual std::string get_var(std::string_view ) const override {return {};}
    virtual Positions load_positions(std::string_view ) const override {return {};}
    virtual Trades load_closed(Timestamp , std::string_view ) const override {return {};}
    virtual VarSet<std::string_view> get_vars(std::string_view ) const override {return {};}
    virtual VarSet<std::string_view> get_vars(std::string_view , std::string_view ) const override {return {};}
    virtual void series_erase_points(std::string_view , uint64_t ) override {};
    virtual uint64_t series_add_point(std::string_view , std::string_view ) override {return 0;}
    virtual ValueStream<std::string_view> load_series(std::string_view ) const override {return{};}
};


}
